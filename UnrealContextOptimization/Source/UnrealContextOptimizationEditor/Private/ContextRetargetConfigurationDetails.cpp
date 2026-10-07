#include "ContextRetargetConfigurationDetails.h"

#include "ContextRetargetConfiguration.h"
#include "DesktopPlatformModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "IPropertyUtilities.h"
#include "IDetailGroup.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#include <initializer_list>

#define LOCTEXT_NAMESPACE "ContextRetargetConfigurationDetails"

namespace
{
FString getRelationshipPresetDirectory()
{
    const TSharedPtr<IPlugin> plugin =
        IPluginManager::Get().FindPlugin(TEXT("UnrealContextOptimization"));
    return plugin.IsValid()
        ? FPaths::Combine(
              plugin->GetBaseDir(),
              TEXT("Resources/ContextPointPresets"))
        : FString();
}

TArray<FString> getRelationshipPresetFiles()
{
    TArray<FString> fileNames;
    const FString directory = getRelationshipPresetDirectory();
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

bool readRelationshipSettings(
    const FJsonObject& json,
    FContextDistanceRelationship& relationship,
    FString& error)
{
    double weight = 1.0;
    json.TryGetNumberField(TEXT("weight"), weight);
    if (!FMath::IsFinite(weight) || weight < 0.0)
    {
        error = TEXT("Relationship weight must be finite and non-negative.");
        return false;
    }
    relationship.weight = weight;
    json.TryGetBoolField(TEXT("useDistance"), relationship.useDistance);
    json.TryGetBoolField(TEXT("useDirection"), relationship.useDirection);
    json.TryGetBoolField(TEXT("usePenetration"), relationship.usePenetration);
    json.TryGetBoolField(
        TEXT("useAdaptiveWeight"),
        relationship.useAdaptiveWeight);
    return true;
}

bool readPresetPointNames(
    const FJsonObject& root,
    TArray<FName>& pointNames,
    FString& error)
{
    const TArray<TSharedPtr<FJsonValue>>* pointValues = nullptr;
    if (!root.TryGetArrayField(TEXT("points"), pointValues))
    {
        error = TEXT("Preset JSON has no 'points' array.");
        return false;
    }

    TSet<FName> uniqueNames;
    for (int32 index = 0; index < pointValues->Num(); ++index)
    {
        const TSharedPtr<FJsonObject> point = (*pointValues)[index]->AsObject();
        FString pointName;
        if (!point.IsValid() ||
            !point->TryGetStringField(TEXT("pointName"), pointName) ||
            pointName.IsEmpty())
        {
            error = FString::Printf(
                TEXT("Point at index %d has no valid 'pointName'."),
                index);
            return false;
        }
        const FName name(*pointName);
        if (uniqueNames.Contains(name))
        {
            error = FString::Printf(
                TEXT("Preset contains duplicate point '%s'."),
                *pointName);
            return false;
        }
        uniqueNames.Add(name);
        pointNames.Add(name);
    }
    return true;
}

bool loadRelationships(
    const FString& path,
    const UContextRetargetConfiguration& configuration,
    TArray<FContextDistanceRelationship>& relationships,
    FString& error)
{
    FString jsonText;
    if (!FFileHelper::LoadFileToString(jsonText, *path))
    {
        error = FString::Printf(TEXT("Could not read preset JSON:\n%s"), *path);
        return false;
    }

    TSharedPtr<FJsonObject> root;
    const TSharedRef<TJsonReader<>> reader =
        TJsonReaderFactory<>::Create(jsonText);
    if (!FJsonSerializer::Deserialize(reader, root) || !root.IsValid())
    {
        error = FString::Printf(TEXT("Preset JSON is invalid:\n%s"), *path);
        return false;
    }

    TArray<FName> presetPointNames;
    if (!readPresetPointNames(*root, presetPointNames, error))
    {
        return false;
    }

    TSet<FName> configurationPointNames;
    TMap<FName, FName> targetBoneNames;
    for (const FContextPointPair& point : configuration.contextPoints)
    {
        configurationPointNames.Add(point.pointName);
        targetBoneNames.Add(point.pointName, point.target.boneName);
    }
    TArray<FString> missingPoints;
    for (const FName pointName : presetPointNames)
    {
        if (!configurationPointNames.Contains(pointName))
        {
            missingPoints.Add(pointName.ToString());
        }
    }
    if (!missingPoints.IsEmpty())
    {
        error = FString::Printf(
            TEXT("The configuration is missing %d preset point(s):\n%s"),
            missingPoints.Num(),
            *FString::Join(missingPoints, TEXT("\n")));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* relationshipValues = nullptr;
    if (!root->TryGetArrayField(TEXT("relationships"), relationshipValues))
    {
        error = TEXT("Preset JSON has no 'relationships' array.");
        return false;
    }

    for (int32 index = 0; index < relationshipValues->Num(); ++index)
    {
        const TSharedPtr<FJsonObject> relationshipJson =
            (*relationshipValues)[index]->AsObject();
        if (!relationshipJson.IsValid())
        {
            error = FString::Printf(
                TEXT("Relationship at index %d is not an object."),
                index);
            return false;
        }

        FString pairing;
        if (relationshipJson->TryGetStringField(TEXT("pairing"), pairing))
        {
            FString points;
            if (!relationshipJson->TryGetStringField(TEXT("points"), points) ||
                !points.Equals(TEXT("all"), ESearchCase::IgnoreCase))
            {
                error = FString::Printf(
                    TEXT("Generated relationship at index %d must use 'points': 'all'."),
                    index);
                return false;
            }

            const bool ordered = pairing.Equals(
                TEXT("allOrderedPairs"),
                ESearchCase::IgnoreCase);
            const bool unique = pairing.Equals(
                TEXT("allUniquePairs"),
                ESearchCase::IgnoreCase);
            if (!ordered && !unique)
            {
                error = FString::Printf(
                    TEXT("Unknown pairing '%s' at relationship index %d."),
                    *pairing,
                    index);
                return false;
            }

            bool excludeSameTargetBone = false;
            relationshipJson->TryGetBoolField(
                TEXT("excludeSameTargetBone"),
                excludeSameTargetBone);
            if (excludeSameTargetBone)
            {
                for (const FName pointName : presetPointNames)
                {
                    const FName targetBoneName = targetBoneNames.FindRef(pointName);
                    if (targetBoneName.IsNone())
                    {
                        error = FString::Printf(
                            TEXT("Generated relationship at index %d cannot exclude same-target-bone pairs because point '%s' has no target bone. Capture the target points before importing relationships."),
                            index,
                            *pointName.ToString());
                        return false;
                    }
                }
            }

            TArray<TPair<FName, FName>> directionDisabledPairs;
            const TArray<TSharedPtr<FJsonValue>>* disabledPairValues = nullptr;
            if (relationshipJson->TryGetArrayField(
                    TEXT("directionDisabledPairs"),
                    disabledPairValues))
            {
                for (int32 pairIndex = 0;
                     pairIndex < disabledPairValues->Num();
                     ++pairIndex)
                {
                    const TSharedPtr<FJsonObject> pairJson =
                        (*disabledPairValues)[pairIndex]->AsObject();
                    FString firstPoint;
                    FString secondPoint;
                    if (!pairJson.IsValid() ||
                        !pairJson->TryGetStringField(
                            TEXT("firstPoint"),
                            firstPoint) ||
                        !pairJson->TryGetStringField(
                            TEXT("secondPoint"),
                            secondPoint) ||
                        firstPoint.IsEmpty() || secondPoint.IsEmpty())
                    {
                        error = FString::Printf(
                            TEXT("Direction-disabled pair at relationship index %d, pair index %d needs 'firstPoint' and 'secondPoint'."),
                            index,
                            pairIndex);
                        return false;
                    }

                    const FName firstName(*firstPoint);
                    const FName secondName(*secondPoint);
                    if (firstName == secondName ||
                        !presetPointNames.Contains(firstName) ||
                        !presetPointNames.Contains(secondName))
                    {
                        error = FString::Printf(
                            TEXT("Direction-disabled pair at relationship index %d, pair index %d references invalid preset points ('%s' -> '%s')."),
                            index,
                            pairIndex,
                            *firstPoint,
                            *secondPoint);
                        return false;
                    }
                    directionDisabledPairs.Emplace(firstName, secondName);
                }
            }

            for (int32 first = 0; first < presetPointNames.Num(); ++first)
            {
                const int32 secondStart = ordered ? 0 : first + 1;
                for (int32 second = secondStart;
                     second < presetPointNames.Num();
                     ++second)
                {
                    if (first == second)
                    {
                        continue;
                    }
                    if (excludeSameTargetBone &&
                        targetBoneNames.FindRef(presetPointNames[first]) ==
                            targetBoneNames.FindRef(presetPointNames[second]))
                    {
                        continue;
                    }
                    FContextDistanceRelationship relationship;
                    relationship.firstPoint = presetPointNames[first];
                    relationship.secondPoint = presetPointNames[second];
                    if (!readRelationshipSettings(
                            *relationshipJson,
                            relationship,
                            error))
                    {
                        return false;
                    }
                    const bool directionDisabled =
                        directionDisabledPairs.ContainsByPredicate(
                            [&relationship](
                                const TPair<FName, FName>& pair)
                            {
                                return
                                    (pair.Key == relationship.firstPoint &&
                                     pair.Value == relationship.secondPoint) ||
                                    (pair.Key == relationship.secondPoint &&
                                     pair.Value == relationship.firstPoint);
                            });
                    if (directionDisabled)
                    {
                        relationship.useDirection = false;
                    }
                    relationships.Add(relationship);
                }
            }
            continue;
        }

        FString firstPoint;
        FString secondPoint;
        if (!relationshipJson->TryGetStringField(TEXT("firstPoint"), firstPoint) ||
            !relationshipJson->TryGetStringField(TEXT("secondPoint"), secondPoint) ||
            firstPoint.IsEmpty() || secondPoint.IsEmpty())
        {
            error = FString::Printf(
                TEXT("Relationship at index %d needs 'firstPoint' and 'secondPoint'."),
                index);
            return false;
        }

        FContextDistanceRelationship relationship;
        relationship.firstPoint = FName(*firstPoint);
        relationship.secondPoint = FName(*secondPoint);
        if (!configurationPointNames.Contains(relationship.firstPoint) ||
            !configurationPointNames.Contains(relationship.secondPoint))
        {
            error = FString::Printf(
                TEXT("Relationship at index %d references a missing context point."),
                index);
            return false;
        }
        if (!readRelationshipSettings(*relationshipJson, relationship, error))
        {
            return false;
        }
        relationships.Add(relationship);
    }
    return true;
}

TSharedRef<FJsonObject> vectorToJson(const FVector& value)
{
    TSharedRef<FJsonObject> result = MakeShared<FJsonObject>();
    result->SetNumberField(TEXT("x"), value.X);
    result->SetNumberField(TEXT("y"), value.Y);
    result->SetNumberField(TEXT("z"), value.Z);
    return result;
}

TSharedRef<FJsonObject> attachmentToJson(const FContextPointAttachment& attachment)
{
    TSharedRef<FJsonObject> result = MakeShared<FJsonObject>();
    result->SetStringField(TEXT("boneName"), attachment.boneName.ToString());
    result->SetObjectField(TEXT("localPosition"), vectorToJson(attachment.localPosition));
    result->SetObjectField(TEXT("localNormal"), vectorToJson(attachment.localNormal));
    return result;
}

FString assetPath(const UObject* asset)
{
    return IsValid(asset) ? asset->GetPathName() : FString();
}

bool writeConfigurationJson(
    const UContextRetargetConfiguration& configuration,
    FString& outputPath,
    FString& errorMessage)
{
    TSharedRef<FJsonObject> root = MakeShared<FJsonObject>();
    root->SetStringField(
        TEXT("sourceSkeletalMesh"),
        assetPath(configuration.sourceSkeletalMesh));
    root->SetStringField(
        TEXT("targetSkeletalMesh"),
        assetPath(configuration.targetSkeletalMesh));
    if (IsValid(configuration.sourceSkeletalMesh))
    {
        root->SetNumberField(
            TEXT("computedSourceCharacterHeight"),
            configuration.sourceSkeletalMesh->GetBounds().BoxExtent.Z * 2.0);
    }
    if (IsValid(configuration.targetSkeletalMesh))
    {
        root->SetNumberField(
            TEXT("computedTargetCharacterHeight"),
            configuration.targetSkeletalMesh->GetBounds().BoxExtent.Z * 2.0);
    }

    TArray<TSharedPtr<FJsonValue>> points;
    for (const FContextPointPair& point : configuration.contextPoints)
    {
        TSharedRef<FJsonObject> pointJson = MakeShared<FJsonObject>();
        pointJson->SetStringField(TEXT("pointName"), point.pointName.ToString());
        pointJson->SetObjectField(TEXT("source"), attachmentToJson(point.source));
        pointJson->SetObjectField(TEXT("target"), attachmentToJson(point.target));
        points.Add(MakeShared<FJsonValueObject>(pointJson));
    }
    root->SetArrayField(TEXT("contextPoints"), points);

    TArray<TSharedPtr<FJsonValue>> relationships;
    for (const FContextDistanceRelationship& relationship :
         configuration.distanceRelationships)
    {
        TSharedRef<FJsonObject> relationshipJson = MakeShared<FJsonObject>();
        relationshipJson->SetStringField(
            TEXT("firstPoint"), relationship.firstPoint.ToString());
        relationshipJson->SetStringField(
            TEXT("secondPoint"), relationship.secondPoint.ToString());
        relationshipJson->SetNumberField(TEXT("weight"), relationship.weight);
        relationshipJson->SetBoolField(TEXT("useDistance"), relationship.useDistance);
        relationshipJson->SetBoolField(TEXT("useDirection"), relationship.useDirection);
        relationshipJson->SetBoolField(TEXT("usePenetration"), relationship.usePenetration);
        relationshipJson->SetBoolField(
            TEXT("useAdaptiveWeight"),
            relationship.useAdaptiveWeight);
        relationships.Add(MakeShared<FJsonValueObject>(relationshipJson));
    }
    root->SetArrayField(TEXT("distanceRelationships"), relationships);

    TArray<TSharedPtr<FJsonValue>> degreesOfFreedom;
    for (const FBoneRotationDegreeOfFreedom& degreeOfFreedom :
         configuration.boneRotationDegreesOfFreedom)
    {
        TSharedRef<FJsonObject> degreeJson = MakeShared<FJsonObject>();
        degreeJson->SetStringField(
            TEXT("boneName"), degreeOfFreedom.boneName.ToString());
        degreeJson->SetBoolField(TEXT("x"), degreeOfFreedom.x);
        degreeJson->SetBoolField(TEXT("y"), degreeOfFreedom.y);
        degreeJson->SetBoolField(TEXT("z"), degreeOfFreedom.z);
        degreesOfFreedom.Add(MakeShared<FJsonValueObject>(degreeJson));
    }
    root->SetArrayField(TEXT("boneRotationDegreesOfFreedom"), degreesOfFreedom);

    const FContextOptimizationSettings& settings = configuration.solverSettings;
    TSharedRef<FJsonObject> settingsJson = MakeShared<FJsonObject>();
    settingsJson->SetNumberField(TEXT("distanceWeight"), settings.distanceWeight);
    settingsJson->SetNumberField(TEXT("directionWeight"), settings.directionWeight);
    settingsJson->SetNumberField(TEXT("penetrationWeight"), settings.penetrationWeight);
    settingsJson->SetNumberField(TEXT("heightWeight"), settings.heightWeight);
    settingsJson->SetNumberField(
        TEXT("pointPositionRegularizationWeight"),
        settings.pointPositionRegularizationWeight);
    settingsJson->SetBoolField(
        TEXT("useTemporalSmoothing"),
        settings.useTemporalSmoothing);
    settingsJson->SetNumberField(TEXT("pointJerkWeight"), settings.pointJerkWeight);
    settingsJson->SetNumberField(
        TEXT("temporalWindowDurationSeconds"),
        settings.temporalWindowDurationSeconds);
    settingsJson->SetNumberField(
        TEXT("temporalWindowOverlapSeconds"),
        settings.temporalWindowOverlapSeconds);
    settingsJson->SetBoolField(TEXT("useAdaptiveWeights"), settings.useAdaptiveWeights);
    settingsJson->SetBoolField(
        TEXT("useTargetAdaptiveWeights"),
        settings.useTargetAdaptiveWeights);
    settingsJson->SetNumberField(
        TEXT("interactionMinimumDistanceRatio"),
        settings.interactionMinimumDistanceRatio);
    settingsJson->SetNumberField(
        TEXT("interactionMaximumDistanceRatio"),
        settings.interactionMaximumDistanceRatio);
    settingsJson->SetNumberField(
        TEXT("floorMinimumHeightRatio"),
        settings.floorMinimumHeightRatio);
    settingsJson->SetNumberField(
        TEXT("floorMaximumHeightRatio"),
        settings.floorMaximumHeightRatio);
    settingsJson->SetObjectField(TEXT("worldUp"), vectorToJson(settings.worldUp));
    settingsJson->SetNumberField(
        TEXT("sourceGroundHeight"),
        settings.sourceGroundHeight);
    settingsJson->SetNumberField(
        TEXT("targetGroundHeight"),
        settings.targetGroundHeight);
    settingsJson->SetNumberField(
        TEXT("absoluteGradientStep"), settings.absoluteGradientStep);
    settingsJson->SetNumberField(
        TEXT("relativeGradientStep"), settings.relativeGradientStep);
    settingsJson->SetNumberField(TEXT("learningRate"), settings.learningRate);
    settingsJson->SetNumberField(TEXT("beta1"), settings.beta1);
    settingsJson->SetNumberField(TEXT("beta2"), settings.beta2);
    settingsJson->SetNumberField(TEXT("epsilon"), settings.epsilon);
    settingsJson->SetNumberField(TEXT("maxIterations"), settings.maxIterations);
    settingsJson->SetNumberField(
        TEXT("gradientTolerance"), settings.gradientTolerance);
    settingsJson->SetNumberField(
        TEXT("minimumRelativeLossImprovement"),
        settings.minimumRelativeLossImprovement);
    settingsJson->SetNumberField(
        TEXT("lossImprovementPatience"),
        settings.lossImprovementPatience);
    settingsJson->SetNumberField(
        TEXT("iterationLogInterval"),
        settings.iterationLogInterval);
    root->SetObjectField(TEXT("solverSettings"), settingsJson);

    FString json;
    const TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&json);
    if (!FJsonSerializer::Serialize(root, writer))
    {
        errorMessage = TEXT("Could not serialize the configuration to JSON.");
        return false;
    }

    const FString outputDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("ContextOptimization"));
    IFileManager::Get().MakeDirectory(*outputDirectory, true);
    outputPath = FPaths::Combine(
        outputDirectory,
        configuration.GetName() + TEXT(".json"));
    if (!FFileHelper::SaveStringToFile(
            json,
            *outputPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        errorMessage = FString::Printf(
            TEXT("Could not write '%s'."),
            *outputPath);
        return false;
    }
    return true;
}
}

TSharedRef<IDetailCustomization> FContextRetargetConfigurationDetails::makeInstance()
{
    return MakeShared<FContextRetargetConfigurationDetails>();
}

void FContextRetargetConfigurationDetails::CustomizeDetails(
    IDetailLayoutBuilder& detailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> customizedObjects;
    detailBuilder.GetObjectsBeingCustomized(customizedObjects);
    for (const TWeakObjectPtr<UObject>& object : customizedObjects)
    {
        if (UContextRetargetConfiguration* configuration =
                Cast<UContextRetargetConfiguration>(object.Get()))
        {
            configurations.Add(configuration);
        }
    }
    propertyUtilities = detailBuilder.GetPropertyUtilities();

    IDetailCategoryBuilder& category = detailBuilder.EditCategory(
        TEXT("Configuration Tools"),
        LOCTEXT("ConfigurationTools", "Configuration Tools"),
        ECategoryPriority::Important);
    category.AddCustomRow(LOCTEXT("ConfigurationActions", "Configuration actions"))
        .WholeRowContent()
        [
            SNew(SUniformGridPanel)
            .SlotPadding(FMargin(2.0f))
            + SUniformGridPanel::Slot(0, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("RemovePoint", "Remove Point And Relationships"))
                .OnClicked(this, &FContextRetargetConfigurationDetails::removePoint)
            ]
            + SUniformGridPanel::Slot(1, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("ExportJson", "Export Configuration JSON"))
                .OnClicked(this, &FContextRetargetConfigurationDetails::exportJson)
            ]
            + SUniformGridPanel::Slot(0, 1)
            [
                SNew(SComboButton)
                .OnGetMenuContent(
                    this,
                    &FContextRetargetConfigurationDetails::buildRelationshipPresetMenu)
                .ButtonContent()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "ImportRelationships",
                        "Import Relationships From Preset"))
                ]
            ]
        ];

    const TSharedRef<IPropertyHandle> solverSettings = detailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UContextRetargetConfiguration, solverSettings));
    detailBuilder.HideProperty(solverSettings);
    IDetailCategoryBuilder& solverCategory = detailBuilder.EditCategory(
        TEXT("Solver"),
        LOCTEXT("Solver", "Solver"));

    const auto addSettingsGroup =
        [&solverCategory, &solverSettings](
            FName groupName,
            const FText& displayName,
            std::initializer_list<FName> propertyNames)
        {
            IDetailGroup& group = solverCategory.AddGroup(groupName, displayName);
            for (const FName propertyName : propertyNames)
            {
                const TSharedPtr<IPropertyHandle> property =
                    solverSettings->GetChildHandle(propertyName);
                if (property.IsValid())
                {
                    group.AddPropertyRow(property.ToSharedRef());
                }
            }
        };

    addSettingsGroup(
        TEXT("Loss"),
        LOCTEXT("LossSettings", "Loss"),
        {
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, distanceWeight),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, directionWeight),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, penetrationWeight),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, heightWeight),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                pointPositionRegularizationWeight)
        });
    addSettingsGroup(
        TEXT("Smoothing"),
        LOCTEXT("SmoothingSettings", "Smoothing"),
        {
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, useTemporalSmoothing),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, pointJerkWeight),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                temporalWindowDurationSeconds),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                temporalWindowOverlapSeconds)
        });
    addSettingsGroup(
        TEXT("AdaptiveWeights"),
        LOCTEXT("AdaptiveWeightSettings", "Adaptive Weights"),
        {
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, useAdaptiveWeights),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                useTargetAdaptiveWeights),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                interactionMinimumDistanceRatio),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                interactionMaximumDistanceRatio),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, floorMinimumHeightRatio),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, floorMaximumHeightRatio)
        });
    addSettingsGroup(
        TEXT("Environment"),
        LOCTEXT("EnvironmentSettings", "Environment"),
        {
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, worldUp),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, sourceGroundHeight),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, targetGroundHeight)
        });
    addSettingsGroup(
        TEXT("NumericalGradient"),
        LOCTEXT("NumericalGradientSettings", "Numerical Gradient"),
        {
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, absoluteGradientStep),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, relativeGradientStep)
        });
    addSettingsGroup(
        TEXT("Adam"),
        LOCTEXT("AdamSettings", "Adam"),
        {
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, learningRate),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, beta1),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, beta2),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, epsilon)
        });
    addSettingsGroup(
        TEXT("Iterations"),
        LOCTEXT("IterationSettings", "Iterations"),
        {
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, maxIterations),
            GET_MEMBER_NAME_CHECKED(FContextOptimizationSettings, gradientTolerance),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                minimumRelativeLossImprovement),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                lossImprovementPatience),
            GET_MEMBER_NAME_CHECKED(
                FContextOptimizationSettings,
                iterationLogInterval)
        });
}

TSharedRef<SWidget>
FContextRetargetConfigurationDetails::buildRelationshipPresetMenu()
{
    FMenuBuilder menuBuilder(true, nullptr);
    for (const FString& presetPath : getRelationshipPresetFiles())
    {
        menuBuilder.AddMenuEntry(
            FText::FromString(FPaths::GetBaseFilename(presetPath)),
            FText::FromString(presetPath),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(
                this,
                &FContextRetargetConfigurationDetails::importRelationships,
                presetPath)));
    }
    menuBuilder.AddMenuSeparator();
    menuBuilder.AddMenuEntry(
        LOCTEXT("BrowseRelationshipPreset", "Browse For JSON..."),
        LOCTEXT(
            "BrowseRelationshipPresetTooltip",
            "Select a context-point preset JSON file."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(
            this,
            &FContextRetargetConfigurationDetails::browseForRelationshipPreset)));
    return menuBuilder.MakeWidget();
}

void FContextRetargetConfigurationDetails::browseForRelationshipPreset()
{
    IDesktopPlatform* desktopPlatform = FDesktopPlatformModule::Get();
    if (desktopPlatform == nullptr)
    {
        return;
    }

    TArray<FString> selectedFiles;
    desktopPlatform->OpenFileDialog(
        FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        TEXT("Select Relationship Preset"),
        getRelationshipPresetDirectory(),
        FString(),
        TEXT("JSON files (*.json)|*.json"),
        EFileDialogFlags::None,
        selectedFiles);
    if (!selectedFiles.IsEmpty())
    {
        importRelationships(selectedFiles[0]);
    }
}

void FContextRetargetConfigurationDetails::importRelationships(
    FString presetPath)
{
    const FScopedTransaction transaction(LOCTEXT(
        "ImportRelationshipsTransaction",
        "Import context point relationships"));
    for (const TWeakObjectPtr<UContextRetargetConfiguration>& weakConfiguration :
         configurations)
    {
        UContextRetargetConfiguration* configuration = weakConfiguration.Get();
        if (configuration == nullptr)
        {
            continue;
        }

        TArray<FContextDistanceRelationship> relationships;
        FString error;
        if (!loadRelationships(
                presetPath,
                *configuration,
                relationships,
                error))
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(error));
            continue;
        }

        const int32 previousCount = configuration->distanceRelationships.Num();
        configuration->Modify();
        configuration->distanceRelationships = MoveTemp(relationships);
        configuration->MarkPackageDirty();

        const FString result = FString::Printf(
            TEXT("Imported %d relationship(s) from '%s'.\nReplaced %d existing relationship(s)."),
            configuration->distanceRelationships.Num(),
            *presetPath,
            previousCount);
        UE_LOG(LogTemp, Display, TEXT("%s"), *result);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(result));
    }

    if (const TSharedPtr<IPropertyUtilities> utilities = propertyUtilities.Pin())
    {
        utilities->ForceRefresh();
    }
}

FReply FContextRetargetConfigurationDetails::removePoint()
{
    for (const TWeakObjectPtr<UContextRetargetConfiguration>& configuration :
         configurations)
    {
        if (!configuration.IsValid())
        {
            continue;
        }
        if (configuration->pointNameToRemove.IsNone())
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                LOCTEXT("MissingPointName", "Set Point Name To Remove first."));
            continue;
        }

        configuration->Modify();
        const FName pointName = configuration->pointNameToRemove;
        const int32 removedPoints = configuration->contextPoints.RemoveAll(
            [pointName](const FContextPointPair& point)
            {
                return point.pointName == pointName;
            });
        const int32 removedRelationships =
            configuration->distanceRelationships.RemoveAll(
                [pointName](const FContextDistanceRelationship& relationship)
                {
                    return relationship.firstPoint == pointName ||
                        relationship.secondPoint == pointName;
                });
        configuration->pointNameToRemove = NAME_None;
        configuration->MarkPackageDirty();

        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::Format(
                LOCTEXT(
                    "RemoveResult",
                    "Removed {0} point pair(s) and {1} relationship(s)."),
                FText::AsNumber(removedPoints),
                FText::AsNumber(removedRelationships)));
    }

    if (const TSharedPtr<IPropertyUtilities> utilities = propertyUtilities.Pin())
    {
        utilities->ForceRefresh();
    }
    return FReply::Handled();
}

FReply FContextRetargetConfigurationDetails::exportJson()
{
    for (const TWeakObjectPtr<UContextRetargetConfiguration>& configuration :
         configurations)
    {
        if (!configuration.IsValid())
        {
            continue;
        }

        FString outputPath;
        FString errorMessage;
        if (!writeConfigurationJson(*configuration, outputPath, errorMessage))
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(errorMessage));
            continue;
        }
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::Format(
                LOCTEXT("ExportResult", "Exported configuration to:\n{0}"),
                FText::FromString(outputPath)));
    }
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
