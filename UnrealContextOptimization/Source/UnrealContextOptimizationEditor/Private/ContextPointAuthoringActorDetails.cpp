#include "ContextPointAuthoringActorDetails.h"

#include "Algo/Reverse.h"
#include "ContextPointAuthoringActor.h"
#include "ContextPointComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "IDesktopPlatform.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigAutoCharacterizer.h"
#include "RigEditor/IKRigController.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ContextPointAuthoringActorDetails"

namespace
{
struct FPointPresetEntry
{
    FName pointName;
    FName referenceBone;
};

struct FSurfaceMatch
{
    FVector normal = FVector::ZeroVector;
    double distanceSquared = TNumericLimits<double>::Max();
    bool found = false;
};

bool isBlueprintAuthoringContext(const AContextPointAuthoringActor& actor)
{
    const UWorld* world = actor.GetWorld();
    return actor.IsTemplate() ||
        (world != nullptr && world->WorldType == EWorldType::EditorPreview);
}

UBlueprint* getActorBlueprint(const AContextPointAuthoringActor& actor)
{
    return Cast<UBlueprint>(actor.GetClass()->ClassGeneratedBy);
}

FString getPointPresetDirectory()
{
    const TSharedPtr<IPlugin> plugin =
        IPluginManager::Get().FindPlugin(TEXT("UnrealContextOptimization"));
    return plugin.IsValid()
        ? FPaths::Combine(
              plugin->GetBaseDir(),
              TEXT("Resources/ContextPointPresets"))
        : FString();
}

TArray<FString> getPointPresetFiles()
{
    TArray<FString> fileNames;
    const FString directory = getPointPresetDirectory();
    if (!directory.IsEmpty())
    {
        IFileManager::Get().FindFiles(
            fileNames,
            *FPaths::Combine(directory, TEXT("*.json")),
            true,
            false);
    }
    fileNames.Sort();
    for (FString& fileName : fileNames)
    {
        fileName = FPaths::Combine(directory, fileName);
    }
    return fileNames;
}

bool loadPreset(
    const FString& path,
    TArray<FPointPresetEntry>& entries,
    FString& error)
{
    FString json;
    if (!FFileHelper::LoadFileToString(json, *path))
    {
        error = FString::Printf(TEXT("Could not read preset JSON:\n%s"), *path);
        return false;
    }

    TSharedPtr<FJsonObject> root;
    const TSharedRef<TJsonReader<>> reader = TJsonReaderFactory<>::Create(json);
    if (!FJsonSerializer::Deserialize(reader, root) || !root.IsValid())
    {
        error = FString::Printf(TEXT("Preset JSON is invalid:\n%s"), *path);
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* pointValues = nullptr;
    if (!root->TryGetArrayField(TEXT("points"), pointValues))
    {
        error = TEXT("Preset JSON has no 'points' array.");
        return false;
    }

    TSet<FName> pointNames;
    for (int32 index = 0; index < pointValues->Num(); ++index)
    {
        const TSharedPtr<FJsonObject> point = (*pointValues)[index]->AsObject();
        FString pointName;
        FString referenceBone;
        if (!point.IsValid() ||
            !point->TryGetStringField(TEXT("pointName"), pointName) ||
            !point->TryGetStringField(TEXT("referenceBone"), referenceBone) ||
            pointName.IsEmpty() || referenceBone.IsEmpty())
        {
            error = FString::Printf(
                TEXT("Point at index %d must have non-empty 'pointName' and 'referenceBone' fields."),
                index);
            return false;
        }

        const FName name(*pointName);
        if (pointNames.Contains(name))
        {
            error = FString::Printf(TEXT("Preset contains duplicate point '%s'."), *pointName);
            return false;
        }
        pointNames.Add(name);
        entries.Add({name, FName(*referenceBone)});
    }
    return true;
}

const FBoneChain* findChain(
    const FRetargetDefinition& definition,
    const FName chainName)
{
    return definition.BoneChains.FindByPredicate(
        [chainName](const FBoneChain& chain)
        {
            return chain.ChainName == chainName;
        });
}

TArray<FName> getChainPath(
    const FReferenceSkeleton& skeleton,
    const FBoneChain& chain)
{
    TArray<FName> reversedPath;
    const int32 startIndex = skeleton.FindBoneIndex(chain.StartBone.BoneName);
    int32 boneIndex = skeleton.FindBoneIndex(chain.EndBone.BoneName);
    while (boneIndex != INDEX_NONE)
    {
        reversedPath.Add(skeleton.GetBoneName(boneIndex));
        if (boneIndex == startIndex)
        {
            Algo::Reverse(reversedPath);
            return reversedPath;
        }
        boneIndex = skeleton.GetParentIndex(boneIndex);
    }
    return {};
}

FName resolveFromChain(
    const FReferenceSkeleton& skeleton,
    const FRetargetDefinition& definition,
    const FName chainName,
    const float position)
{
    const FBoneChain* chain = findChain(definition, chainName);
    if (chain == nullptr)
    {
        return NAME_None;
    }
    const TArray<FName> path = getChainPath(skeleton, *chain);
    if (path.IsEmpty())
    {
        return NAME_None;
    }
    const int32 index = FMath::Clamp(
        FMath::RoundToInt(position * static_cast<float>(path.Num() - 1)),
        0,
        path.Num() - 1);
    return path[index];
}

FName resolveUsingRetargetDefinition(
    const FName referenceBone,
    const FReferenceSkeleton& skeleton,
    const FRetargetDefinition& definition)
{
    if (referenceBone == TEXT("Hip"))
    {
        return definition.RootBone;
    }
    if (referenceBone == TEXT("Waist"))
    {
        return resolveFromChain(skeleton, definition, TEXT("Spine"), 0.0f);
    }
    if (referenceBone == TEXT("Spine02"))
    {
        return resolveFromChain(skeleton, definition, TEXT("Spine"), 1.0f);
    }
    if (referenceBone == TEXT("NeckTwist01"))
    {
        return resolveFromChain(skeleton, definition, TEXT("Neck"), 0.0f);
    }
    if (referenceBone == TEXT("NeckTwist02"))
    {
        return resolveFromChain(skeleton, definition, TEXT("Neck"), 1.0f);
    }
    if (referenceBone == TEXT("Head"))
    {
        return resolveFromChain(skeleton, definition, TEXT("Head"), 1.0f);
    }
    if (referenceBone == TEXT("L_Clavicle"))
    {
        return resolveFromChain(skeleton, definition, TEXT("LeftClavicle"), 0.0f);
    }
    if (referenceBone == TEXT("R_Clavicle"))
    {
        return resolveFromChain(skeleton, definition, TEXT("RightClavicle"), 0.0f);
    }
    if (referenceBone == TEXT("L_Upperarm"))
    {
        return resolveFromChain(skeleton, definition, TEXT("LeftArm"), 0.0f);
    }
    if (referenceBone == TEXT("R_Upperarm"))
    {
        return resolveFromChain(skeleton, definition, TEXT("RightArm"), 0.0f);
    }
    if (referenceBone == TEXT("L_Forearm"))
    {
        return resolveFromChain(skeleton, definition, TEXT("LeftArm"), 0.5f);
    }
    if (referenceBone == TEXT("R_Forearm"))
    {
        return resolveFromChain(skeleton, definition, TEXT("RightArm"), 0.5f);
    }
    if (referenceBone == TEXT("L_Hand"))
    {
        return resolveFromChain(skeleton, definition, TEXT("LeftArm"), 1.0f);
    }
    if (referenceBone == TEXT("R_Hand"))
    {
        return resolveFromChain(skeleton, definition, TEXT("RightArm"), 1.0f);
    }
    return NAME_None;
}

FName resolveBone(
    const FName referenceBone,
    const USkeletalMesh& mesh,
    const FRetargetDefinition* retargetDefinition)
{
    const FReferenceSkeleton& skeleton = mesh.GetRefSkeleton();
    if (skeleton.FindBoneIndex(referenceBone) != INDEX_NONE)
    {
        return referenceBone;
    }

    const FString referenceName = referenceBone.ToString();
    for (int32 index = 0; index < skeleton.GetNum(); ++index)
    {
        const FName candidate = skeleton.GetBoneName(index);
        if (candidate.ToString().EndsWith(referenceName, ESearchCase::IgnoreCase))
        {
            return candidate;
        }
    }

    if (retargetDefinition != nullptr)
    {
        const FName resolved = resolveUsingRetargetDefinition(
            referenceBone,
            skeleton,
            *retargetDefinition);
        if (skeleton.FindBoneIndex(resolved) != INDEX_NONE)
        {
            return resolved;
        }
    }
    return NAME_None;
}

TSet<FName> getExistingPointNames(const AContextPointAuthoringActor& actor)
{
    TSet<FName> names;
    if (isBlueprintAuthoringContext(actor))
    {
        const UBlueprint* blueprint = getActorBlueprint(actor);
        if (blueprint != nullptr && blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* node : blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (const UContextPointComponent* point =
                        Cast<UContextPointComponent>(node->ComponentTemplate))
                {
                    names.Add(point->pointName);
                }
            }
        }
    }
    else
    {
        TArray<UContextPointComponent*> points;
        actor.GetComponents<UContextPointComponent>(points);
        for (const UContextPointComponent* point : points)
        {
            names.Add(point->pointName);
        }
    }
    return names;
}

TArray<UContextPointComponent*> getPointComponents(
    AContextPointAuthoringActor& actor)
{
    TArray<UContextPointComponent*> points;
    if (isBlueprintAuthoringContext(actor))
    {
        const UBlueprint* blueprint = getActorBlueprint(actor);
        if (blueprint != nullptr && blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* node : blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (UContextPointComponent* point =
                        Cast<UContextPointComponent>(node->ComponentTemplate))
                {
                    points.Add(point);
                }
            }
        }
        return points;
    }

    actor.GetComponents<UContextPointComponent>(points);
    return points;
}

TArray<FTransform> getReferenceBoneTransforms(const FReferenceSkeleton& skeleton)
{
    const TArray<FTransform>& localTransforms = skeleton.GetRefBonePose();
    TArray<FTransform> componentTransforms;
    componentTransforms.SetNum(localTransforms.Num());
    for (int32 boneIndex = 0; boneIndex < localTransforms.Num(); ++boneIndex)
    {
        componentTransforms[boneIndex] = localTransforms[boneIndex];
        const int32 parentIndex = skeleton.GetParentIndex(boneIndex);
        if (parentIndex != INDEX_NONE)
        {
            componentTransforms[boneIndex] *= componentTransforms[parentIndex];
        }
    }
    return componentTransforms;
}

FSurfaceMatch findNearestSurface(
    const FVector& point,
    const FSkeletalMeshLODRenderData& lodData,
    const TArray<uint32>& indices)
{
    FSurfaceMatch result;
    const int32 vertexCount = lodData.GetNumVertices();
    for (int32 index = 0; index + 2 < indices.Num(); index += 3)
    {
        const uint32 indexA = indices[index];
        const uint32 indexB = indices[index + 1];
        const uint32 indexC = indices[index + 2];
        if (indexA >= static_cast<uint32>(vertexCount) ||
            indexB >= static_cast<uint32>(vertexCount) ||
            indexC >= static_cast<uint32>(vertexCount))
        {
            continue;
        }

        const FVector positionA = FVector(
            lodData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(indexA));
        const FVector positionB = FVector(
            lodData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(indexB));
        const FVector positionC = FVector(
            lodData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(indexC));
        const FVector closest = FMath::ClosestPointOnTriangleToPoint(
            point,
            positionA,
            positionB,
            positionC);
        const double distanceSquared = FVector::DistSquared(point, closest);
        if (distanceSquared >= result.distanceSquared)
        {
            continue;
        }

        FVector barycentric;
        if (!FMath::ComputeBarycentricTri(
                closest,
                positionA,
                positionB,
                positionC,
                barycentric))
        {
            continue;
        }

        const FVector normalA = FVector(
            lodData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(indexA));
        const FVector normalB = FVector(
            lodData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(indexB));
        const FVector normalC = FVector(
            lodData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(indexC));
        FVector normal =
            normalA * barycentric.X +
            normalB * barycentric.Y +
            normalC * barycentric.Z;
        if (!normal.Normalize())
        {
            normal = FVector::CrossProduct(
                positionB - positionA,
                positionC - positionA).GetSafeNormal();
        }
        if (normal.IsNearlyZero())
        {
            continue;
        }

        result.normal = normal;
        result.distanceSquared = distanceSquared;
        result.found = true;
    }
    return result;
}

void addPointNode(
    AContextPointAuthoringActor& actor,
    UBlueprint& blueprint,
    const FPointPresetEntry& entry,
    const FName boneName)
{
    const FName componentName = FBlueprintEditorUtils::FindUniqueKismetName(
        &blueprint,
        entry.pointName.ToString());
    USCS_Node* node = blueprint.SimpleConstructionScript->CreateNode(
        UContextPointComponent::StaticClass(),
        componentName);
    blueprint.SimpleConstructionScript->AddNode(node);
    node->SetParent(actor.skeletalMeshComponent);
    node->AttachToName = boneName;

    UContextPointComponent* point = CastChecked<UContextPointComponent>(
        node->ComponentTemplate);
    point->pointName = entry.pointName;
    point->boneName = boneName;
    point->SetRelativeLocation(FVector::ZeroVector);
    point->SetRelativeRotation(FRotator::ZeroRotator);
    point->SetRelativeScale3D(FVector(0.025));
}
}

TSharedRef<IDetailCustomization> FContextPointAuthoringActorDetails::makeInstance()
{
    return MakeShared<FContextPointAuthoringActorDetails>();
}

void FContextPointAuthoringActorDetails::CustomizeDetails(
    IDetailLayoutBuilder& detailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> customizedObjects;
    detailBuilder.GetObjectsBeingCustomized(customizedObjects);
    for (const TWeakObjectPtr<UObject>& object : customizedObjects)
    {
        if (AContextPointAuthoringActor* actor =
                Cast<AContextPointAuthoringActor>(object.Get()))
        {
            authoringActors.Add(actor);
        }
    }

    IDetailCategoryBuilder& category = detailBuilder.EditCategory(
        TEXT("Context Point Authoring"),
        LOCTEXT("AuthoringCategory", "Context Point Authoring"),
        ECategoryPriority::Important);

    category.AddCustomRow(LOCTEXT("AuthoringActions", "Context point actions"))
        .WholeRowContent()
        [
            SNew(SUniformGridPanel)
            .SlotPadding(FMargin(2.0f))
            + SUniformGridPanel::Slot(0, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("AddPoint", "Add Context Point"))
                .OnClicked(this, &FContextPointAuthoringActorDetails::runAction,
                    &AContextPointAuthoringActor::addContextPoint)
            ]
            + SUniformGridPanel::Slot(1, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("CapturePoints", "Capture Points To Configuration"))
                .OnClicked(this, &FContextPointAuthoringActorDetails::runAction,
                    &AContextPointAuthoringActor::capturePointsToConfiguration)
            ]
            + SUniformGridPanel::Slot(2, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("ClearPoints", "Clear Context Points"))
                .OnClicked(this, &FContextPointAuthoringActorDetails::runAction,
                    &AContextPointAuthoringActor::clearContextPoints)
            ]
            + SUniformGridPanel::Slot(0, 1)
            [
                SNew(SButton)
                .Text(LOCTEXT("PlayPreview", "Play Preview Animation"))
                .OnClicked(this, &FContextPointAuthoringActorDetails::playPreview)
            ]
            + SUniformGridPanel::Slot(1, 1)
            [
                SNew(SButton)
                .Text(LOCTEXT("RefreshAttachments", "Refresh Point Attachments"))
                .OnClicked(this, &FContextPointAuthoringActorDetails::runAction,
                    &AContextPointAuthoringActor::refreshPointAttachments)
            ]
            + SUniformGridPanel::Slot(2, 1)
            [
                SNew(SButton)
                .Text(LOCTEXT("StopPreview", "Stop Preview Animation"))
                .OnClicked(this, &FContextPointAuthoringActorDetails::stopPreview)
            ]
            + SUniformGridPanel::Slot(0, 2)
            [
                SNew(SComboButton)
                .OnGetMenuContent(
                    this,
                    &FContextPointAuthoringActorDetails::buildPresetMenu)
                .ButtonContent()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("LoadPreset", "Import Points From Preset"))
                ]
            ]
            + SUniformGridPanel::Slot(1, 2)
            [
                SNew(SButton)
                .Text(LOCTEXT("RotatePoints", "Rotate Points To Surface Normals"))
                .OnClicked(this, &FContextPointAuthoringActorDetails::rotatePointsToSurfaceNormals)
            ]
        ];
}

FReply FContextPointAuthoringActorDetails::rotatePointsToSurfaceNormals()
{
    const FScopedTransaction transaction(LOCTEXT(
        "RotatePointsTransaction",
        "Rotate context points to surface normals"));

    for (const TWeakObjectPtr<AContextPointAuthoringActor>& weakActor : authoringActors)
    {
        AContextPointAuthoringActor* actor = weakActor.Get();
        if (actor == nullptr || actor->skeletalMeshComponent == nullptr)
        {
            continue;
        }

        USkeletalMesh* mesh = actor->skeletalMeshComponent->GetSkeletalMeshAsset();
        const FSkeletalMeshRenderData* renderData =
            mesh != nullptr ? mesh->GetResourceForRendering() : nullptr;
        if (mesh == nullptr || renderData == nullptr || renderData->LODRenderData.IsEmpty())
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
                "SurfaceNormalsNeedMesh",
                "Assign a skeletal mesh with LOD 0 render data first."));
            continue;
        }

        const TArray<UContextPointComponent*> points = getPointComponents(*actor);
        if (points.IsEmpty())
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
                "SurfaceNormalsNeedPoints",
                "There are no context points to rotate."));
            continue;
        }

        const FSkeletalMeshLODRenderData& lodData = renderData->LODRenderData[0];
        TArray<uint32> indices;
        lodData.MultiSizeIndexContainer.GetIndexBuffer(indices);
        if (indices.IsEmpty())
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
                "SurfaceNormalsNeedTriangles",
                "The skeletal mesh LOD 0 has no readable triangles."));
            continue;
        }

        UBlueprint* blueprint = isBlueprintAuthoringContext(*actor)
            ? getActorBlueprint(*actor)
            : nullptr;
        actor->Modify();
        if (blueprint != nullptr)
        {
            blueprint->Modify();
            blueprint->SimpleConstructionScript->Modify();
        }

        const FReferenceSkeleton& skeleton = mesh->GetRefSkeleton();
        const TArray<FTransform> boneTransforms =
            getReferenceBoneTransforms(skeleton);
        const double characterHeight = mesh->GetBounds().BoxExtent.Z * 2.0;
        const double maximumDistance = FMath::Max(1.0, characterHeight * 0.03);
        const double maximumDistanceSquared = FMath::Square(maximumDistance);
        TArray<FString> skipped;
        int32 rotatedCount = 0;

        for (UContextPointComponent* point : points)
        {
            const int32 boneIndex = skeleton.FindBoneIndex(point->boneName);
            if (boneIndex == INDEX_NONE || !boneTransforms.IsValidIndex(boneIndex))
            {
                skipped.Add(FString::Printf(
                    TEXT("%s: bone '%s' was not found"),
                    *point->pointName.ToString(),
                    *point->boneName.ToString()));
                continue;
            }

            const FTransform& boneTransform = boneTransforms[boneIndex];
            const FVector pointInMeshSpace = boneTransform.TransformPosition(
                point->GetRelativeLocation());
            const FSurfaceMatch surface = findNearestSurface(
                pointInMeshSpace,
                lodData,
                indices);
            if (!surface.found || surface.distanceSquared > maximumDistanceSquared)
            {
                const double distance = surface.found
                    ? FMath::Sqrt(surface.distanceSquared)
                    : 0.0;
                skipped.Add(FString::Printf(
                    TEXT("%s: nearest surface is %.2f cm away"),
                    *point->pointName.ToString(),
                    distance));
                continue;
            }

            const FVector normalInBoneSpace = boneTransform
                .InverseTransformVectorNoScale(surface.normal)
                .GetSafeNormal();
            if (normalInBoneSpace.IsNearlyZero())
            {
                skipped.Add(FString::Printf(
                    TEXT("%s: surface normal is invalid"),
                    *point->pointName.ToString()));
                continue;
            }

            point->Modify();
            point->SetRelativeRotation(
                FRotationMatrix::MakeFromX(normalInBoneSpace).Rotator());
            ++rotatedCount;
        }

        if (blueprint != nullptr && rotatedCount > 0)
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(blueprint);
        }
        else if (rotatedCount > 0)
        {
            actor->MarkPackageDirty();
        }

        FString result = FString::Printf(
            TEXT("Rotated %d context point(s) to LOD 0 surface normals."),
            rotatedCount);
        if (!skipped.IsEmpty())
        {
            result += FString::Printf(
                TEXT("\n\nSkipped %d point(s):\n%s"),
                skipped.Num(),
                *FString::Join(skipped, TEXT("\n")));
        }
        UE_LOG(LogTemp, Display, TEXT("%s"), *result);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(result));
    }
    return FReply::Handled();
}

TSharedRef<SWidget> FContextPointAuthoringActorDetails::buildPresetMenu()
{
    FMenuBuilder menuBuilder(true, nullptr);
    for (const FString& presetPath : getPointPresetFiles())
    {
        menuBuilder.AddMenuEntry(
            FText::FromString(FPaths::GetBaseFilename(presetPath)),
            FText::FromString(presetPath),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(
                this,
                &FContextPointAuthoringActorDetails::loadContextPointPreset,
                presetPath)));
    }
    menuBuilder.AddMenuSeparator();
    menuBuilder.AddMenuEntry(
        LOCTEXT("BrowsePointPreset", "Browse For JSON..."),
        LOCTEXT("BrowsePointPresetTooltip", "Select a context-point preset JSON file."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(
            this,
            &FContextPointAuthoringActorDetails::browseForContextPointPreset)));
    return menuBuilder.MakeWidget();
}

void FContextPointAuthoringActorDetails::browseForContextPointPreset()
{
    IDesktopPlatform* desktopPlatform = FDesktopPlatformModule::Get();
    if (desktopPlatform == nullptr)
    {
        return;
    }

    TArray<FString> selectedFiles;
    desktopPlatform->OpenFileDialog(
        FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        TEXT("Select Context Point Preset"),
        getPointPresetDirectory(),
        FString(),
        TEXT("JSON files (*.json)|*.json"),
        EFileDialogFlags::None,
        selectedFiles);
    if (!selectedFiles.IsEmpty())
    {
        loadContextPointPreset(selectedFiles[0]);
    }
}

void FContextPointAuthoringActorDetails::loadContextPointPreset(
    FString presetPath)
{
    for (const TWeakObjectPtr<AContextPointAuthoringActor>& weakActor : authoringActors)
    {
        AContextPointAuthoringActor* actor = weakActor.Get();
        if (actor == nullptr || actor->skeletalMeshComponent == nullptr)
        {
            continue;
        }

        USkeletalMesh* mesh = actor->skeletalMeshComponent->GetSkeletalMeshAsset();
        if (mesh == nullptr)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
                "PresetNeedsMesh",
                "Assign a skeletal mesh before loading a context point preset."));
            continue;
        }

        TArray<FPointPresetEntry> entries;
        FString error;
        if (presetPath.IsEmpty() || !loadPreset(presetPath, entries, error))
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(error));
            continue;
        }

        UIKRigDefinition* rig = NewObject<UIKRigDefinition>(GetTransientPackage());
        UIKRigController* controller = UIKRigController::GetController(rig);
        FAutoCharacterizeResults characterizeResults;
        const bool hasRetargetDefinition =
            controller != nullptr &&
            controller->SetSkeletalMesh(mesh);
        if (hasRetargetDefinition)
        {
            controller->AutoGenerateRetargetDefinition(characterizeResults);
        }
        const FRetargetDefinition* retargetDefinition = hasRetargetDefinition
            ? &characterizeResults.AutoRetargetDefinition.RetargetDefinition
            : nullptr;

        const TSet<FName> existingNames = getExistingPointNames(*actor);
        TArray<TPair<FPointPresetEntry, FName>> resolvedEntries;
        TArray<FString> unresolved;
        int32 skippedExisting = 0;
        for (const FPointPresetEntry& entry : entries)
        {
            if (existingNames.Contains(entry.pointName))
            {
                ++skippedExisting;
                continue;
            }
            const FName boneName = resolveBone(
                entry.referenceBone,
                *mesh,
                retargetDefinition);
            if (boneName.IsNone())
            {
                unresolved.Add(FString::Printf(
                    TEXT("%s (%s)"),
                    *entry.pointName.ToString(),
                    *entry.referenceBone.ToString()));
                continue;
            }
            resolvedEntries.Emplace(entry, boneName);
        }

        if (isBlueprintAuthoringContext(*actor))
        {
            UBlueprint* blueprint = getActorBlueprint(*actor);
            if (blueprint == nullptr || blueprint->SimpleConstructionScript == nullptr)
            {
                FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
                    "PresetNeedsBlueprint",
                    "Open an Actor Blueprint derived from ContextPointAuthoringActor."));
                continue;
            }

            const FScopedTransaction transaction(LOCTEXT(
                "LoadPointPresetTransaction",
                "Load context point preset"));
            blueprint->Modify();
            blueprint->SimpleConstructionScript->Modify();
            for (const TPair<FPointPresetEntry, FName>& resolved : resolvedEntries)
            {
                addPointNode(*actor, *blueprint, resolved.Key, resolved.Value);
            }
            if (!resolvedEntries.IsEmpty())
            {
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(blueprint);
            }
        }
        else
        {
            const FName oldPointName = actor->newPointName;
            const FName oldPointBone = actor->newPointBone;
            for (const TPair<FPointPresetEntry, FName>& resolved : resolvedEntries)
            {
                actor->newPointName = resolved.Key.pointName;
                actor->newPointBone = resolved.Value;
                actor->addContextPoint();
            }
            actor->newPointName = oldPointName;
            actor->newPointBone = oldPointBone;
        }

        FString result = FString::Printf(
            TEXT("Loaded %d point(s) from '%s'.\nSkipped %d existing point(s)."),
            resolvedEntries.Num(),
            *presetPath,
            skippedExisting);
        if (characterizeResults.bUsedTemplate)
        {
            result += FString::Printf(
                TEXT("\nUnreal matched skeleton template: %s (%.1f%%)."),
                *characterizeResults.BestTemplateName.ToString(),
                characterizeResults.BestPercentageOfTemplateScore * 100.0f);
        }
        if (!unresolved.IsEmpty())
        {
            result += FString::Printf(
                TEXT("\n\nCould not resolve %d point(s):\n%s"),
                unresolved.Num(),
                *FString::Join(unresolved, TEXT("\n")));
        }
        UE_LOG(LogTemp, Display, TEXT("%s"), *result);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(result));
    }
}

FReply FContextPointAuthoringActorDetails::runAction(ActorAction action)
{
    for (const TWeakObjectPtr<AContextPointAuthoringActor>& actor : authoringActors)
    {
        if (actor.IsValid())
        {
            (actor.Get()->*action)();
        }
    }
    return FReply::Handled();
}

AContextPointAuthoringActor* FContextPointAuthoringActorDetails::findPreviewActor(
    const AContextPointAuthoringActor& templateActor) const
{
    for (TObjectIterator<AContextPointAuthoringActor> actor; actor; ++actor)
    {
        const UWorld* world = actor->GetWorld();
        if (!actor->IsTemplate() &&
            actor->GetClass() == templateActor.GetClass() &&
            world != nullptr &&
            world->WorldType == EWorldType::EditorPreview)
        {
            return *actor;
        }
    }
    return nullptr;
}

FReply FContextPointAuthoringActorDetails::playPreview()
{
    for (const TWeakObjectPtr<AContextPointAuthoringActor>& actor : authoringActors)
    {
        if (!actor.IsValid())
        {
            continue;
        }

        AContextPointAuthoringActor* target = actor.Get();
        if (target->IsTemplate())
        {
            target = findPreviewActor(*target);
        }
        if (target == nullptr)
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                LOCTEXT("NoPreviewActor", "The Blueprint preview actor is not available."));
            continue;
        }

        target->previewAnimation = actor->previewAnimation;
        target->playPreviewAnimation();
    }
    return FReply::Handled();
}

FReply FContextPointAuthoringActorDetails::stopPreview()
{
    for (const TWeakObjectPtr<AContextPointAuthoringActor>& actor : authoringActors)
    {
        if (!actor.IsValid())
        {
            continue;
        }

        AContextPointAuthoringActor* target = actor.Get();
        if (target->IsTemplate())
        {
            target = findPreviewActor(*target);
        }
        if (target != nullptr)
        {
            target->stopPreviewAnimation();
        }
    }
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
