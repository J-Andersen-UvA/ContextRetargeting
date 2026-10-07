#include "ContextPointAuthoringActor.h"

#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "ContextPointComponent.h"
#include "ContextRetargetConfiguration.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/MessageDialog.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#endif

namespace
{
void showAuthoringError(const FText& message)
{
    UE_LOG(LogTemp, Warning, TEXT("%s"), *message.ToString());
#if WITH_EDITOR
    FMessageDialog::Open(EAppMsgType::Ok, message);
#endif
}

void showAuthoringMessage(const FText& message)
{
    UE_LOG(LogTemp, Display, TEXT("%s"), *message.ToString());
#if WITH_EDITOR
    FMessageDialog::Open(EAppMsgType::Ok, message);
#endif
}

#if WITH_EDITOR
UBlueprint* getActorBlueprint(const AContextPointAuthoringActor& actor)
{
    return Cast<UBlueprint>(actor.GetClass()->ClassGeneratedBy);
}

bool isBlueprintAuthoringContext(const AContextPointAuthoringActor& actor)
{
    const UWorld* world = actor.GetWorld();
    return actor.IsTemplate() ||
        (world != nullptr && world->WorldType == EWorldType::EditorPreview);
}

void addPointToBlueprint(
    AContextPointAuthoringActor& actor,
    UBlueprint& blueprint)
{
    const FScopedTransaction transaction(
        FText::FromString(TEXT("Add context point")));
    blueprint.Modify();
    blueprint.SimpleConstructionScript->Modify();

    const FName componentName = FBlueprintEditorUtils::FindUniqueKismetName(
        &blueprint,
        actor.newPointName.ToString());
    USCS_Node* node = blueprint.SimpleConstructionScript->CreateNode(
        UContextPointComponent::StaticClass(),
        componentName);
    blueprint.SimpleConstructionScript->AddNode(node);
    node->SetParent(actor.skeletalMeshComponent);
    node->AttachToName = actor.newPointBone;

    UContextPointComponent* point = CastChecked<UContextPointComponent>(
        node->ComponentTemplate);
    point->pointName = actor.newPointName;
    point->boneName = actor.newPointBone;
    point->SetRelativeLocation(FVector::ZeroVector);
    point->SetRelativeRotation(FRotator::ZeroRotator);
    point->SetRelativeScale3D(FVector(0.025));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&blueprint);
}

TArray<UContextPointComponent*> getBlueprintPointTemplates(
    const AContextPointAuthoringActor& actor)
{
    TArray<UContextPointComponent*> result;
    const UBlueprint* blueprint = getActorBlueprint(actor);
    if (blueprint == nullptr || blueprint->SimpleConstructionScript == nullptr)
    {
        return result;
    }

    for (USCS_Node* node : blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (UContextPointComponent* point =
                Cast<UContextPointComponent>(node->ComponentTemplate))
        {
            result.Add(point);
        }
    }
    return result;
}
#endif

TArray<UContextPointComponent*> getActorPointComponents(
    AContextPointAuthoringActor& actor)
{
    TArray<UContextPointComponent*> result;
    actor.GetComponents<UContextPointComponent>(result);
#if WITH_EDITOR
    if (result.IsEmpty() && isBlueprintAuthoringContext(actor))
    {
        result = getBlueprintPointTemplates(actor);
    }
#endif
    return result;
}
}

AContextPointAuthoringActor::AContextPointAuthoringActor()
{
#if WITH_EDITORONLY_DATA
    bIsEditorOnlyActor = true;
#endif
    skeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(
        TEXT("SkeletalMesh"));
    SetRootComponent(skeletalMeshComponent);
}

void AContextPointAuthoringActor::addContextPoint()
{
    if (newPointName.IsNone() || newPointBone.IsNone())
    {
        showAuthoringError(FText::FromString(
            TEXT("New Point Name and New Point Bone must both be set.")));
        return;
    }

#if WITH_EDITOR
    if (isBlueprintAuthoringContext(*this))
    {
        UBlueprint* blueprint = getActorBlueprint(*this);
        if (blueprint == nullptr)
        {
            showAuthoringError(FText::FromString(
                TEXT("Create an Actor Blueprint derived from ContextPointAuthoringActor first.")));
            return;
        }
        addPointToBlueprint(*this, *blueprint);
        return;
    }
#endif

    if (GetWorld() == nullptr)
    {
        showAuthoringError(FText::FromString(
            TEXT("The authoring actor has no world or Blueprint viewport.")));
        return;
    }

    const USkeletalMesh* skeletalMesh = skeletalMeshComponent->GetSkeletalMeshAsset();
    if (!IsValid(skeletalMesh))
    {
        showAuthoringError(FText::FromString(
            TEXT("Assign a skeletal mesh to the placed authoring actor first.")));
        return;
    }
    if (skeletalMesh->GetRefSkeleton().FindBoneIndex(newPointBone) == INDEX_NONE)
    {
        showAuthoringError(FText::Format(
            FText::FromString(TEXT("Bone '{0}' does not exist in the assigned skeletal mesh.")),
            FText::FromName(newPointBone)));
        return;
    }

    Modify();
    const FName componentName = MakeUniqueObjectName(
        this,
        UContextPointComponent::StaticClass(),
        newPointName);
    UContextPointComponent* point = NewObject<UContextPointComponent>(
        this,
        componentName,
        RF_Transactional);
    point->pointName = newPointName;
    point->boneName = newPointBone;
    AddInstanceComponent(point);
    point->SetupAttachment(skeletalMeshComponent, point->boneName);
    point->RegisterComponent();
    point->SetRelativeScale3D(FVector(0.025));
    contextPoints.Add(point);
    MarkPackageDirty();
}

void AContextPointAuthoringActor::refreshPointAttachments()
{
#if WITH_EDITOR
    if (isBlueprintAuthoringContext(*this))
    {
        UBlueprint* blueprint = getActorBlueprint(*this);
        if (blueprint != nullptr)
        {
            const TArray<UContextPointComponent*> points =
                getBlueprintPointTemplates(*this);
            if (points.IsEmpty())
            {
                showAuthoringError(FText::FromString(
                    TEXT("This Actor Blueprint has no context point components.")));
                return;
            }
            const FScopedTransaction transaction(
                FText::FromString(TEXT("Refresh context point attachments")));
            blueprint->Modify();
            blueprint->SimpleConstructionScript->Modify();
            for (USCS_Node* node : blueprint->SimpleConstructionScript->GetAllNodes())
            {
                UContextPointComponent* point =
                    Cast<UContextPointComponent>(node->ComponentTemplate);
                if (point != nullptr && !point->boneName.IsNone())
                {
                    node->SetParent(skeletalMeshComponent);
                    node->AttachToName = point->boneName;
                }
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(blueprint);
        }
        return;
    }
#endif

    for (UContextPointComponent* point : getActorPointComponents(*this))
    {
        if (IsValid(point) && !point->boneName.IsNone())
        {
            point->AttachToComponent(
                skeletalMeshComponent,
                FAttachmentTransformRules::KeepRelativeTransform,
                point->boneName);
        }
    }
}

void AContextPointAuthoringActor::capturePointsToConfiguration()
{
    if (!IsValid(configuration))
    {
        showAuthoringError(FText::FromString(
            TEXT("Assign a Context Retarget Configuration first.")));
        return;
    }

    const TArray<UContextPointComponent*> points =
        getActorPointComponents(*this);
    if (points.IsEmpty())
    {
        showAuthoringError(FText::FromString(
            TEXT("There are no context point components to capture.")));
        return;
    }

    configuration->Modify();
    USkeletalMesh* skeletalMesh = skeletalMeshComponent->GetSkeletalMeshAsset();
    if (character == EContextAuthoringCharacter::Source)
    {
        configuration->sourceSkeletalMesh = skeletalMesh;
    }
    else
    {
        configuration->targetSkeletalMesh = skeletalMesh;
    }

    int32 capturedPointCount = 0;
    for (const UContextPointComponent* point : points)
    {
        if (!IsValid(point) || point->pointName.IsNone() || point->boneName.IsNone())
        {
            continue;
        }

        FContextPointPair* pair = configuration->contextPoints.FindByPredicate(
            [point](const FContextPointPair& candidate)
            {
                return candidate.pointName == point->pointName;
            });
        if (pair == nullptr)
        {
            pair = &configuration->contextPoints.AddDefaulted_GetRef();
            pair->pointName = point->pointName;
        }

        FContextPointAttachment& attachment =
            character == EContextAuthoringCharacter::Source
                ? pair->source
                : pair->target;
        attachment.boneName = point->boneName;
        attachment.localPosition = point->GetRelativeLocation();
        attachment.localNormal = point->GetRelativeRotation()
            .RotateVector(FVector::ForwardVector)
            .GetSafeNormal();
        ++capturedPointCount;
    }

    configuration->MarkPackageDirty();
    showAuthoringMessage(FText::Format(
        FText::FromString(TEXT("Captured {0} context point(s) to the configuration.")),
        FText::AsNumber(capturedPointCount)));
}

void AContextPointAuthoringActor::clearContextPoints()
{
#if WITH_EDITOR
    if (isBlueprintAuthoringContext(*this))
    {
        UBlueprint* blueprint = getActorBlueprint(*this);
        if (blueprint != nullptr)
        {
            const FScopedTransaction transaction(
                FText::FromString(TEXT("Clear context points")));
            blueprint->Modify();
            blueprint->SimpleConstructionScript->Modify();
            const TArray<USCS_Node*> nodes =
                blueprint->SimpleConstructionScript->GetAllNodes();
            for (USCS_Node* node : nodes)
            {
                if (node->ComponentTemplate->IsA<UContextPointComponent>())
                {
                    blueprint->SimpleConstructionScript->RemoveNodeAndPromoteChildren(node);
                }
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(blueprint);
        }
        return;
    }
#endif

    Modify();
    for (UContextPointComponent* point : contextPoints)
    {
        if (IsValid(point))
        {
            RemoveInstanceComponent(point);
            point->DestroyComponent();
        }
    }
    contextPoints.Reset();
}

void AContextPointAuthoringActor::playPreviewAnimation()
{
    if (!IsValid(previewAnimation))
    {
        showAuthoringError(FText::FromString(
            TEXT("Assign a preview animation first.")));
        return;
    }

    skeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    skeletalMeshComponent->SetAnimation(previewAnimation);
    skeletalMeshComponent->Play(true);
}

void AContextPointAuthoringActor::stopPreviewAnimation()
{
    skeletalMeshComponent->Stop();
}
