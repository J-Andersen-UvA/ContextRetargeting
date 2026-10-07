#include "ContextOptimizationEditorLibrary.h"
#include "ContextOptimizationDiagnosticsCommandlet.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContextRetargetConfiguration.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/FrameRate.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "context_retargeting/context/context_solver.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <cmath>
#include <set>
#include <utility>

#define LOCTEXT_NAMESPACE "ContextOptimizationEditorLibrary"

DEFINE_LOG_CATEGORY_STATIC(LogContextOptimization, Log, All);

namespace
{
namespace context = context_retargeting::context;

constexpr double unrealCentimetersToMeters = 0.01;
constexpr double metersToUnrealCentimeters = 100.0;

context::Vector3 toContextDirection(const FVector& value)
{
    return {value.X, value.Y, value.Z};
}

context::Vector3 toContextPosition(const FVector& value)
{
    return {
        value.X * unrealCentimetersToMeters,
        value.Y * unrealCentimetersToMeters,
        value.Z * unrealCentimetersToMeters};
}

context::Quaternion toContextQuaternion(const FQuat& value)
{
    const FQuat normalized = value.GetNormalized();
    return {normalized.X, normalized.Y, normalized.Z, normalized.W};
}

context::RigidTransform toContextTransform(const FTransform& value)
{
    return {
        toContextQuaternion(value.GetRotation()),
        toContextPosition(value.GetTranslation())};
}

FQuat toUnrealQuaternion(const context::Quaternion& value)
{
    return FQuat(value.x, value.y, value.z, value.w).GetNormalized();
}

bool setError(FString& errorMessage, const FString& value)
{
    errorMessage = value;
    return false;
}

bool validateMeshAndAnimation(
    const USkeletalMesh* mesh,
    const UAnimSequence* animation,
    const TCHAR* label,
    FString& errorMessage)
{
    if (!IsValid(mesh))
    {
        return setError(
            errorMessage,
            FString::Printf(TEXT("The configuration has no %s skeletal mesh."), label));
    }
    if (!IsValid(animation))
    {
        return setError(
            errorMessage,
            FString::Printf(TEXT("No %s animation was provided."), label));
    }
    if (animation->GetSkeleton() != mesh->GetSkeleton())
    {
        return setError(
            errorMessage,
            FString::Printf(
                TEXT("The %s animation does not use the skeleton assigned to the %s skeletal mesh."),
                label,
                label));
    }
    if (!animation->IsDataModelValid())
    {
        return setError(
            errorMessage,
            FString::Printf(TEXT("The %s animation has no valid animation data model."), label));
    }
    return true;
}

bool validateBone(
    const FReferenceSkeleton& referenceSkeleton,
    FName boneName,
    const FString& use,
    FString& errorMessage)
{
    if (boneName.IsNone() || referenceSkeleton.FindBoneIndex(boneName) == INDEX_NONE)
    {
        return setError(
            errorMessage,
            FString::Printf(TEXT("%s references missing bone '%s'."), *use, *boneName.ToString()));
    }
    return true;
}

context::CharacterDefinition createCharacterDefinition(
    const USkeletalMesh& mesh,
    const TArray<FContextPointPair>& points,
    bool source)
{
    context::CharacterDefinition character;
    character.height =
        mesh.GetBounds().BoxExtent.Z * 2.0 * unrealCentimetersToMeters;
    const FReferenceSkeleton& referenceSkeleton = mesh.GetRefSkeleton();
    const int32 boneCount = referenceSkeleton.GetNum();
    character.hierarchy.bones.resize(boneCount);
    for (int32 boneIndex = 0; boneIndex < boneCount; ++boneIndex)
    {
        const int32 parentIndex = referenceSkeleton.GetParentIndex(boneIndex);
        character.hierarchy.bones[boneIndex].parent =
            parentIndex == INDEX_NONE
                ? context::invalidBoneIndex
                : static_cast<context::BoneIndex>(parentIndex);
    }

    character.contextPoints.reserve(points.Num());
    for (const FContextPointPair& point : points)
    {
        const FContextPointAttachment& attachment = source ? point.source : point.target;
        character.contextPoints.push_back({
            static_cast<context::BoneIndex>(referenceSkeleton.FindBoneIndex(attachment.boneName)),
            toContextPosition(attachment.localPosition),
            toContextDirection(attachment.localNormal)});
    }
    return character;
}

context::PoseBatch createPoseBatch(
    const USkeletalMesh& mesh,
    const UAnimSequence& animation,
    TArray<TArray<FVector>>* scales)
{
    const IAnimationDataModel* dataModel = animation.GetDataModel();
    const FReferenceSkeleton& referenceSkeleton = mesh.GetRefSkeleton();
    const int32 frameCount = dataModel->GetNumberOfKeys();
    const int32 boneCount = referenceSkeleton.GetNum();

    context::PoseBatch batch;
    batch.secondsPerFrame = dataModel->GetFrameRate().AsInterval();
    batch.frames.resize(frameCount);
    if (scales != nullptr)
    {
        scales->SetNum(frameCount);
    }

    const TArray<FTransform>& referencePose = referenceSkeleton.GetRefBonePose();
    for (int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        context::LocalPose& frame = batch.frames[frameIndex];
        frame.transforms.resize(boneCount);
        if (scales != nullptr)
        {
            (*scales)[frameIndex].SetNum(boneCount);
        }

        for (int32 boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            const FName boneName = referenceSkeleton.GetBoneName(boneIndex);
            const bool hasTrack = dataModel->IsValidBoneTrackName(boneName);
            const FTransform transform = hasTrack
                ? dataModel->GetBoneTrackTransform(boneName, FFrameNumber(frameIndex))
                : referencePose[boneIndex];
            frame.transforms[boneIndex] = toContextTransform(transform);
            if (scales != nullptr)
            {
                (*scales)[frameIndex][boneIndex] = transform.GetScale3D();
            }
        }
    }
    return batch;
}

context::ContextDefinition createContextDefinition(
    const UContextRetargetConfiguration& configuration)
{
    context::ContextDefinition definition;
    definition.sourceCharacter = createCharacterDefinition(
        *configuration.sourceSkeletalMesh,
        configuration.contextPoints,
        true);
    definition.targetCharacter = createCharacterDefinition(
        *configuration.targetSkeletalMesh,
        configuration.contextPoints,
        false);

    const FContextOptimizationSettings& settings = configuration.solverSettings;
    definition.environment.upAxis = toContextDirection(settings.worldUp);
    definition.environment.sourceGroundHeight =
        settings.sourceGroundHeight * unrealCentimetersToMeters;
    definition.environment.targetGroundHeight =
        settings.targetGroundHeight * unrealCentimetersToMeters;
    definition.adaptiveWeights.enabled = settings.useAdaptiveWeights;
    definition.adaptiveWeights.useTargetWeights =
        settings.useTargetAdaptiveWeights;
    definition.adaptiveWeights.interactionMinimumDistanceRatio =
        settings.interactionMinimumDistanceRatio;
    definition.adaptiveWeights.interactionMaximumDistanceRatio =
        settings.interactionMaximumDistanceRatio;
    definition.adaptiveWeights.floorMinimumHeightRatio =
        settings.floorMinimumHeightRatio;
    definition.adaptiveWeights.floorMaximumHeightRatio =
        settings.floorMaximumHeightRatio;

    TMap<FName, context::ContextPointIndex> pointIndices;
    for (int32 pointIndex = 0; pointIndex < configuration.contextPoints.Num(); ++pointIndex)
    {
        pointIndices.Add(
            configuration.contextPoints[pointIndex].pointName,
            static_cast<context::ContextPointIndex>(pointIndex));
    }

    definition.distanceRelationships.reserve(configuration.distanceRelationships.Num());
    for (const FContextDistanceRelationship& relationship :
         configuration.distanceRelationships)
    {
        definition.distanceRelationships.push_back({
            pointIndices.FindChecked(relationship.firstPoint),
            pointIndices.FindChecked(relationship.secondPoint),
            relationship.weight,
            relationship.useDistance,
            relationship.useDirection,
            relationship.usePenetration,
            relationship.useAdaptiveWeight});
    }

    const FReferenceSkeleton& targetSkeleton =
        configuration.targetSkeletalMesh->GetRefSkeleton();
    definition.boneRotationDegreesOfFreedom.reserve(
        configuration.boneRotationDegreesOfFreedom.Num());
    for (const FBoneRotationDegreeOfFreedom& degreeOfFreedom :
         configuration.boneRotationDegreesOfFreedom)
    {
        const context::BoneIndex boneIndex = static_cast<context::BoneIndex>(
            targetSkeleton.FindBoneIndex(degreeOfFreedom.boneName));
        if (degreeOfFreedom.x)
        {
            definition.boneRotationDegreesOfFreedom.push_back(
                {boneIndex, {1.0, 0.0, 0.0}});
        }
        if (degreeOfFreedom.y)
        {
            definition.boneRotationDegreesOfFreedom.push_back(
                {boneIndex, {0.0, 1.0, 0.0}});
        }
        if (degreeOfFreedom.z)
        {
            definition.boneRotationDegreesOfFreedom.push_back(
                {boneIndex, {0.0, 0.0, 1.0}});
        }
    }
    return definition;
}

context::ContextSolverSettings createSolverSettings(
    const FContextOptimizationSettings& settings)
{
    context::ContextSolverSettings result;
    result.lossWeights.distance = settings.distanceWeight;
    result.lossWeights.direction = settings.directionWeight;
    result.lossWeights.penetration = settings.penetrationWeight;
    result.lossWeights.height = settings.heightWeight;
    result.lossWeights.pointPositionRegularization =
        settings.pointPositionRegularizationWeight;
    result.lossWeights.pointJerk =
        settings.useTemporalSmoothing ? settings.pointJerkWeight : 0.0;
    result.numericalGradient.absoluteStep = settings.absoluteGradientStep;
    result.numericalGradient.relativeStep = settings.relativeGradientStep;
    result.adam.learningRate = settings.learningRate;
    result.adam.beta1 = settings.beta1;
    result.adam.beta2 = settings.beta2;
    result.adam.epsilon = settings.epsilon;
    result.optimization.maxIterations = static_cast<std::size_t>(settings.maxIterations);
    result.optimization.gradientTolerance = settings.gradientTolerance;
    result.optimization.minimumRelativeLossImprovement =
        settings.minimumRelativeLossImprovement;
    result.optimization.lossImprovementPatience =
        static_cast<std::size_t>(settings.lossImprovementPatience);
    return result;
}

FString makePackageName(const FString& outputPackagePath, const FString& outputAssetName)
{
    FString path = outputPackagePath;
    path.RemoveFromEnd(TEXT("/"));
    return path + TEXT("/") + outputAssetName;
}

bool validateOutputTarget(
    const FString& outputPackagePath,
    const FString& outputAssetName,
    FString& errorMessage)
{
    if (outputAssetName.IsEmpty())
    {
        return setError(errorMessage, TEXT("The output asset name is empty."));
    }

    const FString packageName = makePackageName(outputPackagePath, outputAssetName);
    FText reason;
    if (!FPackageName::IsValidLongPackageName(packageName, true, &reason))
    {
        return setError(
            errorMessage,
            FString::Printf(
                TEXT("Invalid output package '%s': %s"),
                *packageName,
                *reason.ToString()));
    }
    if (!FPackageName::IsValidObjectPath(
            packageName + TEXT(".") + outputAssetName,
            &reason))
    {
        return setError(
            errorMessage,
            FString::Printf(TEXT("Invalid output asset name: %s"), *reason.ToString()));
    }
    if (FPackageName::DoesPackageExist(packageName) ||
        FindPackage(nullptr, *packageName) != nullptr)
    {
        return setError(
            errorMessage,
            FString::Printf(
                TEXT("An asset package already exists at '%s'. Choose another asset name."),
                *packageName));
    }
    return true;
}

UAnimSequence* duplicateTargetAnimation(
    UAnimSequence& initialTargetAnimation,
    const FString& packageName,
    const FString& assetName,
    FString& errorMessage)
{
    FText packageReason;
    if (!FPackageName::IsValidLongPackageName(packageName, true, &packageReason))
    {
        errorMessage = FString::Printf(
            TEXT("Invalid output package '%s': %s"),
            *packageName,
            *packageReason.ToString());
        return nullptr;
    }
    if (FPackageName::DoesPackageExist(packageName))
    {
        errorMessage = FString::Printf(
            TEXT("An asset package already exists at '%s'. Choose another asset name."),
            *packageName);
        return nullptr;
    }

    UPackage* package = CreatePackage(*packageName);
    UAnimSequence* result = DuplicateObject<UAnimSequence>(
        &initialTargetAnimation,
        package,
        FName(*assetName));
    if (!IsValid(result))
    {
        errorMessage = TEXT("Unreal could not duplicate the initial target animation.");
        return nullptr;
    }
    result->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
    return result;
}

bool writeSolvedTracks(
    UAnimSequence& animation,
    const USkeletalMesh& targetMesh,
    const context::PoseBatch& solvedPoses,
    const TArray<TArray<FVector>>& scales,
    FString& errorMessage)
{
    const FReferenceSkeleton& referenceSkeleton = targetMesh.GetRefSkeleton();
    IAnimationDataModel* dataModel = animation.GetDataModel();
    IAnimationDataController& controller = animation.GetController();
    IAnimationDataController::FScopedBracket bracket(
        controller,
        LOCTEXT("BakeContextOptimization", "Bake context optimization"),
        true);

    const int32 frameCount = static_cast<int32>(solvedPoses.frames.size());
    for (int32 boneIndex = 0; boneIndex < referenceSkeleton.GetNum(); ++boneIndex)
    {
        const FName boneName = referenceSkeleton.GetBoneName(boneIndex);
        if (!dataModel->IsValidBoneTrackName(boneName))
        {
            if (!controller.AddBoneCurve(boneName, true))
            {
                errorMessage = FString::Printf(
                    TEXT("Could not add an animation track for bone '%s'."),
                    *boneName.ToString());
                return false;
            }
        }

        TArray<FVector> translations;
        TArray<FQuat> rotations;
        TArray<FVector> boneScales;
        translations.Reserve(frameCount);
        rotations.Reserve(frameCount);
        boneScales.Reserve(frameCount);
        for (int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const context::RigidTransform& transform =
                solvedPoses.frames[frameIndex].transforms[boneIndex];
            translations.Emplace(
                transform.translation.x * metersToUnrealCentimeters,
                transform.translation.y * metersToUnrealCentimeters,
                transform.translation.z * metersToUnrealCentimeters);
            rotations.Add(toUnrealQuaternion(transform.rotation));
            boneScales.Add(scales[frameIndex][boneIndex]);
        }

        if (!controller.SetBoneTrackKeys(
                boneName,
                translations,
                rotations,
                boneScales,
                true))
        {
            errorMessage = FString::Printf(
                TEXT("Could not write animation keys for bone '%s'."),
                *boneName.ToString());
            return false;
        }
    }
    return true;
}

struct FFrameSolveSummary
{
    context::PoseBatch targetPoses;
    context::ContextLossComponents initialLoss;
    context::ContextLossComponents finalLoss;
    context::ContextGradientMagnitudes initialGradientMagnitudes;
    context::ContextGradientMagnitudes finalGradientMagnitudes;
    int32 maximumIterationsPerWindow = 0;
};

struct FTemporalWindow
{
    int32 startFrame = 0;
    int32 frameCount = 0;
};

TArray<FTemporalWindow> createTemporalWindows(
    int32 frameCount,
    int32 windowFrameCount,
    int32 overlapFrameCount)
{
    TArray<FTemporalWindow> windows;
    if (windowFrameCount >= frameCount)
    {
        windows.Add({0, frameCount});
        return windows;
    }

    const int32 step = windowFrameCount - overlapFrameCount;
    int32 windowStart = 0;
    while (windowStart < frameCount)
    {
        const int32 currentFrameCount = FMath::Min(
            windowFrameCount,
            frameCount - windowStart);
        if (currentFrameCount < 4 && !windows.IsEmpty())
        {
            windows.Last().frameCount = frameCount - windows.Last().startFrame;
            break;
        }
        windows.Add({windowStart, currentFrameCount});
        if (windowStart + currentFrameCount >= frameCount)
        {
            break;
        }
        windowStart += step;
    }
    return windows;
}

double temporalBlendWeight(
    const TArray<FTemporalWindow>& windows,
    int32 windowIndex,
    int32 frameInWindow)
{
    const FTemporalWindow& window = windows[windowIndex];
    double weight = 1.0;
    if (windowIndex > 0)
    {
        const FTemporalWindow& previous = windows[windowIndex - 1];
        const int32 overlap =
            previous.startFrame + previous.frameCount - window.startFrame;
        if (frameInWindow < overlap)
        {
            const double t = static_cast<double>(frameInWindow + 1) /
                static_cast<double>(overlap + 1);
            weight *= t * t * (3.0 - 2.0 * t);
        }
    }
    if (windowIndex + 1 < windows.Num())
    {
        const FTemporalWindow& next = windows[windowIndex + 1];
        const int32 overlap =
            window.startFrame + window.frameCount - next.startFrame;
        const int32 overlapStart = window.frameCount - overlap;
        if (frameInWindow >= overlapStart)
        {
            const double t = static_cast<double>(window.frameCount - frameInWindow) /
                static_cast<double>(overlap + 1);
            weight *= t * t * (3.0 - 2.0 * t);
        }
    }
    return weight;
}

const TCHAR* stopReasonName(
    context_retargeting::optimization::StopReason stopReason)
{
    using context_retargeting::optimization::StopReason;
    switch (stopReason)
    {
    case StopReason::MaxIterations:
        return TEXT("maximum iterations");
    case StopReason::GradientTolerance:
        return TEXT("gradient tolerance");
    case StopReason::LossImprovement:
        return TEXT("relative loss improvement");
    case StopReason::NonFiniteLoss:
        return TEXT("non-finite loss");
    case StopReason::NonFiniteGradient:
        return TEXT("non-finite gradient");
    }
    return TEXT("unknown reason");
}

void logOptimizationProgress(
    const context_retargeting::optimization::OptimizationResult& result,
    const context_retargeting::optimization::OptimizationSettings& settings,
    int32 windowIndex,
    int32 windowCount,
    int32 iterationLogInterval)
{
    const std::size_t interval = static_cast<std::size_t>(iterationLogInterval);
    if (!result.lossHistory.empty() && result.iterations % interval != 0)
    {
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Window %d of %d iteration %llu loss %.9g"),
            windowIndex + 1,
            windowCount,
            static_cast<uint64>(result.iterations),
            result.lossHistory.back());
    }
    if (result.stopReason ==
            context_retargeting::optimization::StopReason::LossImprovement &&
        result.iterations >= settings.lossImprovementPatience)
    {
        const double referenceLoss = result.lossHistory[
            result.lossHistory.size() - 1 - settings.lossImprovementPatience];
        const double currentLoss = result.lossHistory.back();
        const double relativeImprovement =
            (referenceLoss - currentLoss) /
            FMath::Max(FMath::Abs(referenceLoss), 1.0);
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Window %d of %d stopped after %llu iterations: relative loss improvement %.9g over %llu iterations was below %.9g; returned loss %.9g"),
            windowIndex + 1,
            windowCount,
            static_cast<uint64>(result.iterations),
            relativeImprovement,
            static_cast<uint64>(settings.lossImprovementPatience),
            settings.minimumRelativeLossImprovement,
            result.loss);
    }
    else
    {
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Window %d of %d stopped after %llu iterations: %s; returned loss %.9g"),
            windowIndex + 1,
            windowCount,
            static_cast<uint64>(result.iterations),
            stopReasonName(result.stopReason),
            result.loss);
    }
}

bool solveFrames(
    const context::ContextDefinition& definition,
    const context::ContextBatchInput& input,
    const context::ContextSolverSettings& settings,
    double temporalWindowDurationSeconds,
    double temporalWindowOverlapSeconds,
    int32 iterationLogInterval,
    FScopedSlowTask& slowTask,
    FFrameSolveSummary& summary)
{
    const int32 frameCount = static_cast<int32>(input.sourcePoses.frames.size());
    const int32 degreesOfFreedomPerFrame = static_cast<int32>(
        definition.boneRotationDegreesOfFreedom.size());
    const bool useTemporalSmoothing = settings.lossWeights.pointJerk > 0.0;
    const int32 windowFrameCount = useTemporalSmoothing
        ? FMath::Min(
              frameCount,
              FMath::Max(
                  4,
                  FMath::RoundToInt(
                      temporalWindowDurationSeconds /
                      input.sourcePoses.secondsPerFrame)))
        : 1;
    const int32 overlapFrameCount = useTemporalSmoothing
        ? FMath::Clamp(
              FMath::RoundToInt(
                  temporalWindowOverlapSeconds /
                  input.sourcePoses.secondsPerFrame),
              0,
              windowFrameCount - 1)
        : 0;
    const TArray<FTemporalWindow> windows = createTemporalWindows(
        frameCount,
        windowFrameCount,
        overlapFrameCount);
    UE_LOG(
        LogContextOptimization,
        Display,
        TEXT("Optimization schedule: %d window(s), up to %d frames per window, %d requested overlap frames"),
        windows.Num(),
        windowFrameCount,
        overlapFrameCount);
    context_retargeting::optimization::ParameterVector solvedParameters(
        static_cast<std::size_t>(frameCount * degreesOfFreedomPerFrame),
        0.0);
    context_retargeting::optimization::ParameterVector accumulatedWeights(
        static_cast<std::size_t>(frameCount),
        0.0);

    for (int32 windowIndex = 0; windowIndex < windows.Num(); ++windowIndex)
    {
        if (slowTask.ShouldCancel())
        {
            return false;
        }

        slowTask.EnterProgressFrame(
            1.0f,
            FText::Format(
                LOCTEXT("SolveWindow", "Optimizing window {0} of {1}"),
                FText::AsNumber(windowIndex + 1),
                FText::AsNumber(windows.Num())));
        const FTemporalWindow& window = windows[windowIndex];
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Optimizing window %d of %d (frames %d-%d)"),
            windowIndex + 1,
            windows.Num(),
            window.startFrame + 1,
            window.startFrame + window.frameCount);

        context::ContextBatchInput windowInput;
        windowInput.sourcePoses.secondsPerFrame = input.sourcePoses.secondsPerFrame;
        windowInput.initialTargetPoses.secondsPerFrame =
            input.initialTargetPoses.secondsPerFrame;
        windowInput.sourcePoses.frames.reserve(window.frameCount);
        windowInput.initialTargetPoses.frames.reserve(window.frameCount);
        for (int32 frameInWindow = 0;
             frameInWindow < window.frameCount;
             ++frameInWindow)
        {
            const int32 inputFrame = window.startFrame + frameInWindow;
            windowInput.sourcePoses.frames.push_back(input.sourcePoses.frames[inputFrame]);
            windowInput.initialTargetPoses.frames.push_back(
                input.initialTargetPoses.frames[inputFrame]);
        }

        context::ContextSolverSettings windowSettings = settings;
        windowSettings.optimization.progress =
            [windowIndex,
             windowCount = windows.Num(),
             iterationLogInterval,
             maxIterations = windowSettings.optimization.maxIterations,
             useTargetWeights = definition.adaptiveWeights.enabled &&
                 definition.adaptiveWeights.useTargetWeights](
                std::size_t iteration,
                double loss)
            {
                if (iteration % static_cast<std::size_t>(iterationLogInterval) == 0)
                {
                    const double targetWeightMix = !useTargetWeights
                        ? 0.0
                        : maxIterations <= 1
                            ? 1.0
                            : FMath::Min(
                                  static_cast<double>(
                                      iteration > 0 ? iteration - 1 : 0) /
                                      static_cast<double>(maxIterations - 1),
                                  1.0);
                    UE_LOG(
                        LogContextOptimization,
                        Display,
                        TEXT("Window %d of %d iteration %llu loss %.9g | target adaptive weight mix %.3f"),
                        windowIndex + 1,
                        windowCount,
                        static_cast<uint64>(iteration),
                        loss,
                        targetWeightMix);
                }
            };
        context::ContextSolveResult windowResult = context::solveContextBatch(
            definition,
            windowInput,
            windowSettings);
        logOptimizationProgress(
            windowResult.optimization,
            windowSettings.optimization,
            windowIndex,
            windows.Num(),
            iterationLogInterval);
        for (int32 frameInWindow = 0;
             frameInWindow < window.frameCount;
             ++frameInWindow)
        {
            const int32 outputFrame = window.startFrame + frameInWindow;
            const double blendWeight = temporalBlendWeight(
                windows,
                windowIndex,
                frameInWindow);
            accumulatedWeights[static_cast<std::size_t>(outputFrame)] += blendWeight;
            for (int32 degree = 0; degree < degreesOfFreedomPerFrame; ++degree)
            {
                solvedParameters[
                    static_cast<std::size_t>(
                        outputFrame * degreesOfFreedomPerFrame + degree)] +=
                    blendWeight * windowResult.optimization.parameters[
                        static_cast<std::size_t>(
                            frameInWindow * degreesOfFreedomPerFrame + degree)];
            }
        }
        summary.maximumIterationsPerWindow = FMath::Max(
            summary.maximumIterationsPerWindow,
            static_cast<int32>(windowResult.optimization.iterations));
    }

    for (int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        const double weight = accumulatedWeights[static_cast<std::size_t>(frameIndex)];
        for (int32 degree = 0; degree < degreesOfFreedomPerFrame; ++degree)
        {
            solvedParameters[
                static_cast<std::size_t>(
                    frameIndex * degreesOfFreedomPerFrame + degree)] /= weight;
        }
    }

    context::ContextObjective fullObjective(definition, input, settings.lossWeights);
    if (definition.adaptiveWeights.enabled &&
        definition.adaptiveWeights.useTargetWeights)
    {
        fullObjective.setTargetAdaptiveWeightMix(1.0);
    }
    const context_retargeting::optimization::ParameterVector initialParameters(
        solvedParameters.size(),
        0.0);
    summary.initialLoss = fullObjective.evaluateLossComponents(initialParameters);
    summary.finalLoss = fullObjective.evaluateLossComponents(solvedParameters);
    context_retargeting::optimization::ParameterVector gradient(
        solvedParameters.size(),
        0.0);
    summary.initialGradientMagnitudes =
        fullObjective.evaluateCentralDifferenceGradient(
            initialParameters,
            gradient,
            settings.numericalGradient);
    summary.finalGradientMagnitudes =
        fullObjective.evaluateCentralDifferenceGradient(
            solvedParameters,
            gradient,
            settings.numericalGradient);
    summary.targetPoses = fullObjective.createTargetPoses(solvedParameters);
    return true;
}

using DiagnosticPointFrames = std::vector<std::vector<context::Vector3>>;

std::vector<context::Vector3> evaluateDiagnosticFramePoints(
    const context::CharacterDefinition& character,
    const context::LocalPose& pose)
{
    context::GlobalPose globalPose;
    context::calculateGlobalPose(character.hierarchy, pose, globalPose);
    std::vector<context::Vector3> positions(character.contextPoints.size());
    for (std::size_t point = 0; point < character.contextPoints.size(); ++point)
    {
        const context::ContextPointDefinition& pointDefinition =
            character.contextPoints[point];
        positions[point] =
            globalPose.transforms[pointDefinition.parentBone].transformPosition(
                pointDefinition.localPosition);
    }
    return positions;
}

DiagnosticPointFrames evaluateDiagnosticPointFrames(
    const context::CharacterDefinition& character,
    const context::PoseBatch& poses)
{
    DiagnosticPointFrames result;
    result.reserve(poses.frames.size());
    for (const context::LocalPose& pose : poses.frames)
    {
        result.push_back(evaluateDiagnosticFramePoints(character, pose));
    }
    return result;
}

std::vector<context::Vector3> evaluateDiagnosticFrameNormals(
    const context::CharacterDefinition& character,
    const context::LocalPose& pose)
{
    context::GlobalPose globalPose;
    context::calculateGlobalPose(character.hierarchy, pose, globalPose);
    std::vector<context::Vector3> normals(character.contextPoints.size());
    for (std::size_t point = 0; point < character.contextPoints.size(); ++point)
    {
        const context::ContextPointDefinition& pointDefinition =
            character.contextPoints[point];
        normals[point] = globalPose.transforms[pointDefinition.parentBone]
            .rotation
            .rotateVector(pointDefinition.localNormal)
            .normalized();
    }
    return normals;
}

DiagnosticPointFrames evaluateDiagnosticNormalFrames(
    const context::CharacterDefinition& character,
    const context::PoseBatch& poses)
{
    DiagnosticPointFrames result;
    result.reserve(poses.frames.size());
    for (const context::LocalPose& pose : poses.frames)
    {
        result.push_back(evaluateDiagnosticFrameNormals(character, pose));
    }
    return result;
}

struct FRelationshipDiagnosticAggregate
{
    int32 relationshipIndex = INDEX_NONE;
    int32 activeFrames = 0;
    bool sameSourceBone = false;
    bool sameTargetBone = false;
    double initialDistanceLoss = 0.0;
    double optimizedDistanceLoss = 0.0;
    double initialDirectionLoss = 0.0;
    double optimizedDirectionLoss = 0.0;
    double initialPenetrationLoss = 0.0;
    double optimizedPenetrationLoss = 0.0;
};

double quaternionAngularDistance(
    const context::Quaternion& first,
    const context::Quaternion& second)
{
    const double dot =
        first.x * second.x + first.y * second.y +
        first.z * second.z + first.w * second.w;
    return 2.0 * std::acos(FMath::Clamp(FMath::Abs(dot), 0.0, 1.0));
}

context::Vector3 quaternionRotationVector(context::Quaternion rotation)
{
    rotation = rotation.normalized();
    if (rotation.w < 0.0)
    {
        rotation.x = -rotation.x;
        rotation.y = -rotation.y;
        rotation.z = -rotation.z;
        rotation.w = -rotation.w;
    }
    const double vectorLength = std::sqrt(
        rotation.x * rotation.x +
        rotation.y * rotation.y +
        rotation.z * rotation.z);
    if (vectorLength <= 1.0e-12)
    {
        return {0.0, 0.0, 0.0};
    }
    const double angle = 2.0 * std::atan2(vectorLength, rotation.w);
    return {
        rotation.x * angle / vectorLength,
        rotation.y * angle / vectorLength,
        rotation.z * angle / vectorLength};
}

context::Vector3 extractXyzParameters(context::Quaternion rotation)
{
    rotation = rotation.normalized();
    const double matrix00 = 1.0 - 2.0 * (
        rotation.y * rotation.y + rotation.z * rotation.z);
    const double matrix01 = 2.0 * (
        rotation.x * rotation.y - rotation.z * rotation.w);
    const double matrix02 = 2.0 * (
        rotation.x * rotation.z + rotation.y * rotation.w);
    const double matrix12 = 2.0 * (
        rotation.y * rotation.z - rotation.x * rotation.w);
    const double matrix22 = 1.0 - 2.0 * (
        rotation.x * rotation.x + rotation.y * rotation.y);
    const double y = std::asin(FMath::Clamp(matrix02, -1.0, 1.0));
    return {
        std::atan2(-matrix12, matrix22),
        y,
        std::atan2(-matrix01, matrix00)};
}

double vectorNorm(
    const context_retargeting::optimization::ParameterVector& values)
{
    double squaredNorm = 0.0;
    for (const double value : values)
    {
        squaredNorm += value * value;
    }
    return std::sqrt(squaredNorm);
}

double cosineAgreement(
    const context_retargeting::optimization::ParameterVector& first,
    const context_retargeting::optimization::ParameterVector& second)
{
    double dot = 0.0;
    double firstSquaredNorm = 0.0;
    double secondSquaredNorm = 0.0;
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        dot += first[index] * second[index];
        firstSquaredNorm += first[index] * first[index];
        secondSquaredNorm += second[index] * second[index];
    }
    if (firstSquaredNorm == 0.0 || secondSquaredNorm == 0.0)
    {
        return firstSquaredNorm == secondSquaredNorm ? 1.0 : 0.0;
    }
    return FMath::Clamp(
        dot / std::sqrt(firstSquaredNorm * secondSquaredNorm),
        -1.0,
        1.0);
}

double relativeVectorDifference(
    const context_retargeting::optimization::ParameterVector& first,
    const context_retargeting::optimization::ParameterVector& second)
{
    double squaredDifference = 0.0;
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        const double difference = first[index] - second[index];
        squaredDifference += difference * difference;
    }
    const double denominator = FMath::Max(vectorNorm(first), vectorNorm(second));
    return denominator == 0.0 ? 0.0 : std::sqrt(squaredDifference) / denominator;
}

struct NamedGradientVector
{
    const TCHAR* name;
    const context_retargeting::optimization::ParameterVector* values;
};

TArray<NamedGradientVector> namedGradientVectors(
    const context::ContextGradientVectors& gradients)
{
    return {
        {TEXT("distance"), &gradients.distance},
        {TEXT("direction"), &gradients.direction},
        {TEXT("penetration"), &gradients.penetration},
        {TEXT("height"), &gradients.height},
        {TEXT("pointPositionRegularization"),
         &gradients.pointPositionRegularization},
        {TEXT("pointJerk"), &gradients.pointJerk},
        {TEXT("total"), &gradients.total}};
}

const TCHAR* axisName(const context::Vector3& axis)
{
    if (axis.x == 1.0)
    {
        return TEXT("X");
    }
    if (axis.y == 1.0)
    {
        return TEXT("Y");
    }
    return TEXT("Z");
}

double axisParameter(
    const context::Vector3& parameters,
    const context::Vector3& axis)
{
    if (axis.x == 1.0)
    {
        return parameters.x;
    }
    if (axis.y == 1.0)
    {
        return parameters.y;
    }
    return parameters.z;
}

struct UnstableGradient
{
    FString loss;
    int32 frame = 0;
    FString bone;
    FString axis;
    double smallestStepGradient = 0.0;
    double middleStepGradient = 0.0;
    double largestStepGradient = 0.0;
    double maximumAbsoluteGradient = 0.0;
    double relativeRange = 0.0;
    bool signDisagreement = false;
};

bool writeGradientAudit(
    const UAnimSequence& sourceAnimation,
    const UAnimSequence& initialTargetAnimation,
    const UAnimSequence& optimizedAnimation,
    const UContextRetargetConfiguration& configuration,
    int32 requestedStartFrame,
    int32 requestedEndFrame,
    const FString& outputDirectory,
    FString& errorMessage)
{
    const context::PoseBatch sourcePoses = createPoseBatch(
        *configuration.sourceSkeletalMesh,
        sourceAnimation,
        nullptr);
    const context::PoseBatch initialTargetPoses = createPoseBatch(
        *configuration.targetSkeletalMesh,
        initialTargetAnimation,
        nullptr);
    const context::PoseBatch optimizedPoses = createPoseBatch(
        *configuration.targetSkeletalMesh,
        optimizedAnimation,
        nullptr);
    if (sourcePoses.frames.size() != initialTargetPoses.frames.size() ||
        sourcePoses.frames.size() != optimizedPoses.frames.size())
    {
        return setError(errorMessage, TEXT("Gradient-audit animations have different frame counts."));
    }

    const int32 frameCount = static_cast<int32>(sourcePoses.frames.size());
    const int32 startFrame = FMath::Clamp(requestedStartFrame, 0, frameCount - 1);
    const int32 endFrame = FMath::Clamp(requestedEndFrame, startFrame, frameCount - 1);
    context::ContextBatchInput input;
    input.sourcePoses.secondsPerFrame = sourcePoses.secondsPerFrame;
    input.initialTargetPoses.secondsPerFrame = initialTargetPoses.secondsPerFrame;
    for (int32 frame = startFrame; frame <= endFrame; ++frame)
    {
        input.sourcePoses.frames.push_back(sourcePoses.frames[frame]);
        input.initialTargetPoses.frames.push_back(initialTargetPoses.frames[frame]);
    }

    const context::ContextDefinition definition =
        createContextDefinition(configuration);
    const context::ContextSolverSettings solverSettings =
        createSolverSettings(configuration.solverSettings);
    context::ContextObjective objective(
        definition,
        input,
        solverSettings.lossWeights);
    if (definition.adaptiveWeights.enabled &&
        definition.adaptiveWeights.useTargetWeights)
    {
        objective.setTargetAdaptiveWeightMix(1.0);
    }

    const std::size_t degreesPerFrame =
        definition.boneRotationDegreesOfFreedom.size();
    context_retargeting::optimization::ParameterVector parameters(
        input.sourcePoses.frames.size() * degreesPerFrame,
        0.0);
    for (int32 frame = startFrame; frame <= endFrame; ++frame)
    {
        const std::size_t localFrame = static_cast<std::size_t>(frame - startFrame);
        for (std::size_t degree = 0; degree < degreesPerFrame; ++degree)
        {
            const context::BoneRotationDegreeOfFreedom& degreeOfFreedom =
                definition.boneRotationDegreesOfFreedom[degree];
            const context::Quaternion correction =
                (initialTargetPoses.frames[frame]
                     .transforms[degreeOfFreedom.bone]
                     .rotation.inverse() *
                 optimizedPoses.frames[frame]
                     .transforms[degreeOfFreedom.bone]
                     .rotation).normalized();
            parameters[localFrame * degreesPerFrame + degree] = axisParameter(
                extractXyzParameters(correction),
                degreeOfFreedom.localAxis);
        }
    }

    constexpr std::array<double, 3> gradientSteps = {
        0.001,
        0.0001,
        0.00001};
    std::array<context::ContextGradientVectors, 3> gradients;
    std::array<context_retargeting::optimization::ParameterVector, 3>
        totalGradients;
    for (std::size_t stepIndex = 0; stepIndex < gradientSteps.size(); ++stepIndex)
    {
        totalGradients[stepIndex].assign(parameters.size(), 0.0);
        context_retargeting::optimization::CentralDifferenceSettings settings;
        settings.absoluteStep = gradientSteps[stepIndex];
        settings.relativeStep = gradientSteps[stepIndex];
        static_cast<void>(objective.evaluateCentralDifferenceGradient(
            parameters,
            totalGradients[stepIndex],
            settings,
            &gradients[stepIndex]));
    }

    const std::array<TArray<NamedGradientVector>, 3> namedGradients = {
        namedGradientVectors(gradients[0]),
        namedGradientVectors(gradients[1]),
        namedGradientVectors(gradients[2])};
    FString summaryCsv = TEXT("loss,firstStep,secondStep,firstNorm,secondNorm,cosineAgreement,relativeDifference\n");
    constexpr std::array<std::pair<std::size_t, std::size_t>, 3> comparisons = {{
        {0, 1},
        {1, 2},
        {0, 2}}};
    for (std::size_t loss = 0; loss < namedGradients[0].Num(); ++loss)
    {
        for (const auto [first, second] : comparisons)
        {
            const auto& firstValues = *namedGradients[first][loss].values;
            const auto& secondValues = *namedGradients[second][loss].values;
            summaryCsv.Appendf(
                TEXT("%s,%.9g,%.9g,%.17g,%.17g,%.17g,%.17g\n"),
                namedGradients[first][loss].name,
                gradientSteps[first],
                gradientSteps[second],
                vectorNorm(firstValues),
                vectorNorm(secondValues),
                cosineAgreement(firstValues, secondValues),
                relativeVectorDifference(firstValues, secondValues));
        }
    }

    FString alignmentCsv = TEXT("firstLoss,secondLoss,step,cosineAgreement\n");
    const TArray<NamedGradientVector>& middleGradients = namedGradients[1];
    for (int32 first = 0; first < middleGradients.Num() - 1; ++first)
    {
        for (int32 second = first + 1; second < middleGradients.Num() - 1; ++second)
        {
            alignmentCsv.Appendf(
                TEXT("%s,%s,%.9g,%.17g\n"),
                middleGradients[first].name,
                middleGradients[second].name,
                gradientSteps[1],
                cosineAgreement(
                    *middleGradients[first].values,
                    *middleGradients[second].values));
        }
    }

    TArray<UnstableGradient> unstableGradients;
    FString parametersCsv = TEXT("loss,frame,animationFrame,timeSeconds,bone,axis,gradientAt0.001,gradientAt0.0001,gradientAt0.00001,maximumAbsoluteGradient,relativeRange,signDisagreement,unstable\n");
    const FReferenceSkeleton& targetSkeleton =
        configuration.targetSkeletalMesh->GetRefSkeleton();
    for (std::size_t loss = 0; loss < namedGradients[0].Num(); ++loss)
    {
        const double componentNorm = vectorNorm(*namedGradients[1][loss].values);
        for (std::size_t parameter = 0; parameter < parameters.size(); ++parameter)
        {
            const double largestStepGradient =
                (*namedGradients[0][loss].values)[parameter];
            const double middleStepGradient =
                (*namedGradients[1][loss].values)[parameter];
            const double smallestStepGradient =
                (*namedGradients[2][loss].values)[parameter];
            const double minimumGradient = FMath::Min3(
                largestStepGradient,
                middleStepGradient,
                smallestStepGradient);
            const double maximumGradient = FMath::Max3(
                largestStepGradient,
                middleStepGradient,
                smallestStepGradient);
            const double maximumAbsoluteGradient = FMath::Max3(
                FMath::Abs(largestStepGradient),
                FMath::Abs(middleStepGradient),
                FMath::Abs(smallestStepGradient));
            const double relativeRange = maximumAbsoluteGradient == 0.0
                ? 0.0
                : (maximumGradient - minimumGradient) /
                    maximumAbsoluteGradient;
            const bool signDisagreement =
                minimumGradient < 0.0 && maximumGradient > 0.0;
            const bool significant = maximumAbsoluteGradient >=
                FMath::Max(1.0e-12, componentNorm * 1.0e-6);
            const bool unstable = significant &&
                (signDisagreement || relativeRange > 0.05);
            const std::size_t localFrame = parameter / degreesPerFrame;
            const std::size_t degree = parameter % degreesPerFrame;
            const context::BoneRotationDegreeOfFreedom& degreeOfFreedom =
                definition.boneRotationDegreesOfFreedom[degree];
            parametersCsv.Appendf(
                TEXT("%s,%llu,%llu,%.9g,%s,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%d\n"),
                namedGradients[0][loss].name,
                static_cast<uint64>(localFrame),
                static_cast<uint64>(localFrame + startFrame),
                localFrame * input.sourcePoses.secondsPerFrame,
                *targetSkeleton.GetBoneName(degreeOfFreedom.bone).ToString(),
                axisName(degreeOfFreedom.localAxis),
                largestStepGradient,
                middleStepGradient,
                smallestStepGradient,
                maximumAbsoluteGradient,
                relativeRange,
                signDisagreement ? 1 : 0,
                unstable ? 1 : 0);
            if (unstable)
            {
                unstableGradients.Add({
                    namedGradients[0][loss].name,
                    static_cast<int32>(localFrame + startFrame),
                    targetSkeleton.GetBoneName(degreeOfFreedom.bone).ToString(),
                    axisName(degreeOfFreedom.localAxis),
                    smallestStepGradient,
                    middleStepGradient,
                    largestStepGradient,
                    maximumAbsoluteGradient,
                    relativeRange,
                    signDisagreement});
            }
        }
    }
    unstableGradients.Sort([](
        const UnstableGradient& first,
        const UnstableGradient& second)
    {
        return first.relativeRange * first.maximumAbsoluteGradient >
            second.relativeRange * second.maximumAbsoluteGradient;
    });

    IFileManager::Get().MakeDirectory(*outputDirectory, true);
    const bool saved = FFileHelper::SaveStringToFile(
            summaryCsv,
            *FPaths::Combine(outputDirectory, TEXT("gradient_step_summary.csv"))) &&
        FFileHelper::SaveStringToFile(
            parametersCsv,
            *FPaths::Combine(outputDirectory, TEXT("gradient_parameter_stability.csv"))) &&
        FFileHelper::SaveStringToFile(
            alignmentCsv,
            *FPaths::Combine(outputDirectory, TEXT("gradient_loss_alignment.csv")));
    if (!saved)
    {
        return setError(errorMessage, TEXT("Could not write one or more gradient-audit CSV files."));
    }

    UE_LOG(
        LogContextOptimization,
        Display,
        TEXT("Gradient audit used %llu fixed optimized-pose parameters across frames %d-%d; found %d unstable loss-parameter entries."),
        static_cast<uint64>(parameters.size()),
        startFrame,
        endFrame,
        unstableGradients.Num());
    const int32 loggedCount = FMath::Min(20, unstableGradients.Num());
    for (int32 index = 0; index < loggedCount; ++index)
    {
        const UnstableGradient& unstable = unstableGradients[index];
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Unstable gradient %d: %s frame %d %s %s | h=1e-3 %.9g h=1e-4 %.9g h=1e-5 %.9g | relative range %.6g%s"),
            index + 1,
            *unstable.loss,
            unstable.frame,
            *unstable.bone,
            *unstable.axis,
            unstable.largestStepGradient,
            unstable.middleStepGradient,
            unstable.smallestStepGradient,
            unstable.relativeRange,
            unstable.signDisagreement ? TEXT(" sign disagreement") : TEXT(""));
    }
    return true;
}

double diagnosticDirectionError(
    context::Vector3 sourceOffset,
    context::Vector3 targetOffset)
{
    const double sourceLengthSquared = sourceOffset.lengthSquared();
    const double targetLengthSquared = targetOffset.lengthSquared();
    if (sourceLengthSquared <= 0.0 || targetLengthSquared <= 0.0)
    {
        return sourceLengthSquared == targetLengthSquared ? 0.0 : 1.0;
    }
    const double cosine = context::Vector3::dot(sourceOffset, targetOffset) /
        std::sqrt(sourceLengthSquared * targetLengthSquared);
    const double cosineDistance =
        1.0 - FMath::Clamp(cosine, -1.0, 1.0);
    return cosineDistance * cosineDistance;
}

double diagnosticAdaptiveWeight(
    double distance,
    double characterHeight,
    const context::ContextDefinition& definition,
    const context::DistanceRelationship& relationship)
{
    if (!definition.adaptiveWeights.enabled || !relationship.useAdaptiveWeight)
    {
        return 1.0;
    }
    const double minimum =
        characterHeight *
        definition.adaptiveWeights.interactionMinimumDistanceRatio;
    const double maximum =
        characterHeight *
        definition.adaptiveWeights.interactionMaximumDistanceRatio;
    return FMath::Clamp(1.0 - (distance - minimum) / (maximum - minimum), 0.0, 1.0);
}

context::Vector3 pointDifference(
    const DiagnosticPointFrames& positions,
    int32 frame,
    std::size_t point,
    int32 order,
    double secondsPerFrame)
{
    context::Vector3 difference;
    if (order == 1 && frame >= 1)
    {
        difference = positions[frame][point] - positions[frame - 1][point];
    }
    else if (order == 2 && frame >= 2)
    {
        difference = positions[frame][point] -
            2.0 * positions[frame - 1][point] +
            positions[frame - 2][point];
    }
    else if (order == 3 && frame >= 3)
    {
        difference = positions[frame][point] -
            3.0 * positions[frame - 1][point] +
            3.0 * positions[frame - 2][point] -
            positions[frame - 3][point];
    }
    return difference / std::pow(secondsPerFrame, order);
}

bool writeAnimationDiagnostics(
    const UAnimSequence& sourceAnimation,
    const UAnimSequence& initialTargetAnimation,
    const UAnimSequence& optimizedAnimation,
    const UContextRetargetConfiguration& configuration,
    int32 requestedStartFrame,
    int32 requestedEndFrame,
    const FString& outputDirectory,
    FString& errorMessage)
{
    const context::ContextDefinition definition =
        createContextDefinition(configuration);
    const context::PoseBatch sourcePoses = createPoseBatch(
        *configuration.sourceSkeletalMesh,
        sourceAnimation,
        nullptr);
    const context::PoseBatch initialTargetPoses = createPoseBatch(
        *configuration.targetSkeletalMesh,
        initialTargetAnimation,
        nullptr);
    const context::PoseBatch optimizedPoses = createPoseBatch(
        *configuration.targetSkeletalMesh,
        optimizedAnimation,
        nullptr);
    const int32 frameCount = static_cast<int32>(optimizedPoses.frames.size());
    if (sourcePoses.frames.size() != optimizedPoses.frames.size() ||
        initialTargetPoses.frames.size() != optimizedPoses.frames.size())
    {
        return setError(errorMessage, TEXT("Diagnostic animations have different frame counts."));
    }
    const int32 startFrame = FMath::Clamp(requestedStartFrame, 0, frameCount - 1);
    const int32 endFrame = FMath::Clamp(requestedEndFrame, startFrame, frameCount - 1);
    const DiagnosticPointFrames sourcePoints = evaluateDiagnosticPointFrames(
        definition.sourceCharacter,
        sourcePoses);
    const DiagnosticPointFrames initialPoints = evaluateDiagnosticPointFrames(
        definition.targetCharacter,
        initialTargetPoses);
    const DiagnosticPointFrames optimizedPoints = evaluateDiagnosticPointFrames(
        definition.targetCharacter,
        optimizedPoses);
    const DiagnosticPointFrames sourceNormals = evaluateDiagnosticNormalFrames(
        definition.sourceCharacter,
        sourcePoses);
    const DiagnosticPointFrames initialNormals = evaluateDiagnosticNormalFrames(
        definition.targetCharacter,
        initialTargetPoses);
    const DiagnosticPointFrames optimizedNormals = evaluateDiagnosticNormalFrames(
        definition.targetCharacter,
        optimizedPoses);

    IFileManager::Get().MakeDirectory(*outputDirectory, true);
    FString bonesCsv = TEXT("frame,timeSeconds,bone,correctionAngleDegrees,correctionChangeDegrees,initialFrameChangeDegrees,optimizedFrameChangeDegrees,correctionX,correctionY,correctionZ,correctionW,correctionDeltaX,correctionDeltaY,correctionDeltaZ,parameterX,parameterY,parameterZ,parameterDeltaX,parameterDeltaY,parameterDeltaZ\n");
    const FReferenceSkeleton& targetSkeleton =
        configuration.targetSkeletalMesh->GetRefSkeleton();
    std::set<int32> diagnosticBones;
    for (const FBoneRotationDegreeOfFreedom& degreeOfFreedom :
         configuration.boneRotationDegreesOfFreedom)
    {
        diagnosticBones.insert(
            targetSkeleton.FindBoneIndex(degreeOfFreedom.boneName));
    }
    for (int32 frame = startFrame; frame <= endFrame; ++frame)
    {
        for (const int32 boneIndex : diagnosticBones)
        {
            const context::Quaternion& initialRotation =
                initialTargetPoses.frames[frame].transforms[boneIndex].rotation;
            const context::Quaternion& optimizedRotation =
                optimizedPoses.frames[frame].transforms[boneIndex].rotation;
            const context::Quaternion correction =
                (initialRotation.inverse() * optimizedRotation).normalized();
            const context::Vector3 parameterAngles =
                extractXyzParameters(correction);
            double correctionChange = 0.0;
            double initialChange = 0.0;
            double optimizedChange = 0.0;
            context::Vector3 correctionDelta{};
            context::Vector3 parameterDelta{};
            if (frame > 0)
            {
                const context::Quaternion previousCorrection =
                    (initialTargetPoses.frames[frame - 1]
                         .transforms[boneIndex]
                         .rotation.inverse() *
                     optimizedPoses.frames[frame - 1]
                         .transforms[boneIndex]
                         .rotation).normalized();
                correctionChange = quaternionAngularDistance(
                    previousCorrection,
                    correction);
                correctionDelta = quaternionRotationVector(
                    previousCorrection.inverse() * correction);
                parameterDelta = parameterAngles -
                    extractXyzParameters(previousCorrection);
                initialChange = quaternionAngularDistance(
                    initialTargetPoses.frames[frame - 1].transforms[boneIndex].rotation,
                    initialRotation);
                optimizedChange = quaternionAngularDistance(
                    optimizedPoses.frames[frame - 1].transforms[boneIndex].rotation,
                    optimizedRotation);
            }
            bonesCsv.Appendf(
                TEXT("%d,%.9g,%s,%.9g,%.9g,%.9g,%.9g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n"),
                frame,
                frame * optimizedPoses.secondsPerFrame,
                *targetSkeleton.GetBoneName(boneIndex).ToString(),
                FMath::RadiansToDegrees(quaternionAngularDistance(
                    context::Quaternion::identity(),
                    correction)),
                FMath::RadiansToDegrees(correctionChange),
                FMath::RadiansToDegrees(initialChange),
                FMath::RadiansToDegrees(optimizedChange),
                correction.x,
                correction.y,
                correction.z,
                correction.w,
                correctionDelta.x,
                correctionDelta.y,
                correctionDelta.z,
                parameterAngles.x,
                parameterAngles.y,
                parameterAngles.z,
                parameterDelta.x,
                parameterDelta.y,
                parameterDelta.z);
        }
    }

    FString pointsCsv = TEXT("frame,timeSeconds,point,sourceX,sourceY,sourceZ,initialX,initialY,initialZ,optimizedX,optimizedY,optimizedZ,displacement,initialVelocity,optimizedVelocity,initialAcceleration,optimizedAcceleration,initialJerk,optimizedJerk\n");
    for (int32 frame = startFrame; frame <= endFrame; ++frame)
    {
        for (int32 point = 0; point < configuration.contextPoints.Num(); ++point)
        {
            const context::Vector3& source = sourcePoints[frame][point];
            const context::Vector3& initial = initialPoints[frame][point];
            const context::Vector3& optimized = optimizedPoints[frame][point];
            pointsCsv.Appendf(
                TEXT("%d,%.9g,%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n"),
                frame,
                frame * optimizedPoses.secondsPerFrame,
                *configuration.contextPoints[point].pointName.ToString(),
                source.x, source.y, source.z,
                initial.x, initial.y, initial.z,
                optimized.x, optimized.y, optimized.z,
                (optimized - initial).length(),
                pointDifference(
                    initialPoints,
                    frame,
                    point,
                    1,
                    optimizedPoses.secondsPerFrame).length(),
                pointDifference(
                    optimizedPoints,
                    frame,
                    point,
                    1,
                    optimizedPoses.secondsPerFrame).length(),
                pointDifference(
                    initialPoints,
                    frame,
                    point,
                    2,
                    optimizedPoses.secondsPerFrame).length(),
                pointDifference(
                    optimizedPoints,
                    frame,
                    point,
                    2,
                    optimizedPoses.secondsPerFrame).length(),
                pointDifference(
                    initialPoints,
                    frame,
                    point,
                    3,
                    optimizedPoses.secondsPerFrame).length(),
                pointDifference(
                    optimizedPoints,
                    frame,
                    point,
                    3,
                    optimizedPoses.secondsPerFrame).length());
        }
    }

    TArray<FRelationshipDiagnosticAggregate> relationshipAggregates;
    relationshipAggregates.SetNum(configuration.distanceRelationships.Num());
    for (int32 relationshipIndex = 0;
         relationshipIndex < relationshipAggregates.Num();
         ++relationshipIndex)
    {
        const context::DistanceRelationship& relationship =
            definition.distanceRelationships[relationshipIndex];
        FRelationshipDiagnosticAggregate& aggregate =
            relationshipAggregates[relationshipIndex];
        aggregate.relationshipIndex = relationshipIndex;
        aggregate.sameSourceBone =
            definition.sourceCharacter.contextPoints[relationship.firstPoint]
                .parentBone ==
            definition.sourceCharacter.contextPoints[relationship.secondPoint]
                .parentBone;
        aggregate.sameTargetBone =
            definition.targetCharacter.contextPoints[relationship.firstPoint]
                .parentBone ==
            definition.targetCharacter.contextPoints[relationship.secondPoint]
                .parentBone;
    }

    FString framesCsv = TEXT("frame,timeSeconds,sourceActiveRelationships,initialTargetOnlyRelationships,optimizedTargetOnlyRelationships,optimizedActiveRelationships,activeSameSourceBone,activeSameTargetBone,initialDistanceLoss,optimizedDistanceLoss,initialDirectionLoss,optimizedDirectionLoss,initialPenetrationLoss,optimizedPenetrationLoss\n");
    FString relationshipsCsv = TEXT("frame,timeSeconds,index,firstPoint,secondPoint,sameSourceBone,sameTargetBone,sourceDistance,initialDistance,optimizedDistance,sourceAdaptiveWeight,initialTargetAdaptiveWeight,optimizedTargetAdaptiveWeight,initialCombinedAdaptiveWeight,optimizedCombinedAdaptiveWeight,initialEffectiveWeight,optimizedEffectiveWeight,initialTargetOnly,optimizedTargetOnly,initialDistanceError,optimizedDistanceError,initialDistanceLoss,optimizedDistanceLoss,initialDirectionError,optimizedDirectionError,initialDirectionLoss,optimizedDirectionLoss,sourcePenetration,initialPenetration,optimizedPenetration,initialPenetrationLoss,optimizedPenetrationLoss\n");
    for (int32 frame = startFrame; frame <= endFrame; ++frame)
    {
        int32 sourceActiveRelationships = 0;
        int32 initialTargetOnlyRelationships = 0;
        int32 optimizedTargetOnlyRelationships = 0;
        int32 optimizedActiveRelationships = 0;
        int32 activeSameSourceBone = 0;
        int32 activeSameTargetBone = 0;
        double frameInitialDistanceLoss = 0.0;
        double frameOptimizedDistanceLoss = 0.0;
        double frameInitialDirectionLoss = 0.0;
        double frameOptimizedDirectionLoss = 0.0;
        double frameInitialPenetrationLoss = 0.0;
        double frameOptimizedPenetrationLoss = 0.0;
        for (int32 relationshipIndex = 0;
             relationshipIndex < configuration.distanceRelationships.Num();
             ++relationshipIndex)
        {
            const context::DistanceRelationship& relationship =
                definition.distanceRelationships[relationshipIndex];
            const context::Vector3 sourceOffset =
                sourcePoints[frame][relationship.secondPoint] -
                sourcePoints[frame][relationship.firstPoint];
            const context::Vector3 initialOffset =
                initialPoints[frame][relationship.secondPoint] -
                initialPoints[frame][relationship.firstPoint];
            const context::Vector3 optimizedOffset =
                optimizedPoints[frame][relationship.secondPoint] -
                optimizedPoints[frame][relationship.firstPoint];
            const double sourceDistance = sourceOffset.length();
            const double initialDistance = initialOffset.length();
            const double optimizedDistance = optimizedOffset.length();
            const double sourceAdaptiveWeight = diagnosticAdaptiveWeight(
                sourceDistance,
                definition.sourceCharacter.height,
                definition,
                relationship);
            const double initialTargetAdaptiveWeight = diagnosticAdaptiveWeight(
                initialDistance,
                definition.targetCharacter.height,
                definition,
                relationship);
            const double optimizedTargetAdaptiveWeight = diagnosticAdaptiveWeight(
                optimizedDistance,
                definition.targetCharacter.height,
                definition,
                relationship);
            const bool mixTargetWeights =
                definition.adaptiveWeights.enabled &&
                definition.adaptiveWeights.useTargetWeights &&
                relationship.useAdaptiveWeight;
            const double initialCombinedAdaptiveWeight =
                sourceAdaptiveWeight +
                (mixTargetWeights ? initialTargetAdaptiveWeight : 0.0);
            const double optimizedCombinedAdaptiveWeight =
                sourceAdaptiveWeight +
                (mixTargetWeights ? optimizedTargetAdaptiveWeight : 0.0);
            const FContextDistanceRelationship& configuredRelationship =
                configuration.distanceRelationships[relationshipIndex];
            const double initialEffectiveWeight =
                relationship.weight * initialCombinedAdaptiveWeight *
                initialCombinedAdaptiveWeight;
            const double optimizedEffectiveWeight =
                relationship.weight * optimizedCombinedAdaptiveWeight *
                optimizedCombinedAdaptiveWeight;
            const bool initialTargetOnly =
                sourceAdaptiveWeight <= 0.0 && initialTargetAdaptiveWeight > 0.0;
            const bool optimizedTargetOnly =
                sourceAdaptiveWeight <= 0.0 && optimizedTargetAdaptiveWeight > 0.0;
            const double initialDistanceError =
                initialDistance - sourceDistance;
            const double optimizedDistanceError =
                optimizedDistance - sourceDistance;
            const double initialDirectionError =
                diagnosticDirectionError(sourceOffset, initialOffset);
            const double optimizedDirectionError =
                diagnosticDirectionError(sourceOffset, optimizedOffset);
            const double sourcePenetration = context::Vector3::dot(
                sourceNormals[frame][relationship.firstPoint],
                sourceOffset);
            const double initialPenetration = context::Vector3::dot(
                initialNormals[frame][relationship.firstPoint],
                initialOffset);
            const double optimizedPenetration = context::Vector3::dot(
                optimizedNormals[frame][relationship.firstPoint],
                optimizedOffset);
            const double initialPenetrationError =
                initialPenetration - sourcePenetration;
            const double optimizedPenetrationError =
                optimizedPenetration - sourcePenetration;
            const double initialDistanceLoss = relationship.useDistance
                ? initialEffectiveWeight * initialDistanceError * initialDistanceError
                : 0.0;
            const double optimizedDistanceLoss = relationship.useDistance
                ? optimizedEffectiveWeight * optimizedDistanceError * optimizedDistanceError
                : 0.0;
            const double initialDirectionLoss = relationship.useDirection
                ? initialEffectiveWeight * initialDirectionError
                : 0.0;
            const double optimizedDirectionLoss = relationship.useDirection
                ? optimizedEffectiveWeight * optimizedDirectionError
                : 0.0;
            const double initialPenetrationLoss = relationship.usePenetration
                ? initialEffectiveWeight * initialPenetrationError * initialPenetrationError
                : 0.0;
            const double optimizedPenetrationLoss = relationship.usePenetration
                ? optimizedEffectiveWeight * optimizedPenetrationError * optimizedPenetrationError
                : 0.0;
            FRelationshipDiagnosticAggregate& aggregate =
                relationshipAggregates[relationshipIndex];
            sourceActiveRelationships += sourceAdaptiveWeight > 0.0 ? 1 : 0;
            initialTargetOnlyRelationships += initialTargetOnly ? 1 : 0;
            optimizedTargetOnlyRelationships += optimizedTargetOnly ? 1 : 0;
            if (optimizedEffectiveWeight > 0.0)
            {
                ++optimizedActiveRelationships;
                ++aggregate.activeFrames;
                activeSameSourceBone += aggregate.sameSourceBone ? 1 : 0;
                activeSameTargetBone += aggregate.sameTargetBone ? 1 : 0;
            }
            aggregate.initialDistanceLoss += initialDistanceLoss;
            aggregate.optimizedDistanceLoss += optimizedDistanceLoss;
            aggregate.initialDirectionLoss += initialDirectionLoss;
            aggregate.optimizedDirectionLoss += optimizedDirectionLoss;
            aggregate.initialPenetrationLoss += initialPenetrationLoss;
            aggregate.optimizedPenetrationLoss += optimizedPenetrationLoss;
            frameInitialDistanceLoss += initialDistanceLoss;
            frameOptimizedDistanceLoss += optimizedDistanceLoss;
            frameInitialDirectionLoss += initialDirectionLoss;
            frameOptimizedDirectionLoss += optimizedDirectionLoss;
            frameInitialPenetrationLoss += initialPenetrationLoss;
            frameOptimizedPenetrationLoss += optimizedPenetrationLoss;
            relationshipsCsv.Appendf(
                TEXT("%d,%.9g,%d,%s,%s,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n"),
                frame,
                frame * optimizedPoses.secondsPerFrame,
                relationshipIndex,
                *configuredRelationship.firstPoint.ToString(),
                *configuredRelationship.secondPoint.ToString(),
                aggregate.sameSourceBone ? 1 : 0,
                aggregate.sameTargetBone ? 1 : 0,
                sourceDistance,
                initialDistance,
                optimizedDistance,
                sourceAdaptiveWeight,
                initialTargetAdaptiveWeight,
                optimizedTargetAdaptiveWeight,
                initialCombinedAdaptiveWeight,
                optimizedCombinedAdaptiveWeight,
                initialEffectiveWeight,
                optimizedEffectiveWeight,
                initialTargetOnly ? 1 : 0,
                optimizedTargetOnly ? 1 : 0,
                initialDistanceError,
                optimizedDistanceError,
                initialDistanceLoss,
                optimizedDistanceLoss,
                initialDirectionError,
                optimizedDirectionError,
                initialDirectionLoss,
                optimizedDirectionLoss,
                sourcePenetration,
                initialPenetration,
                optimizedPenetration,
                initialPenetrationLoss,
                optimizedPenetrationLoss);
        }
        framesCsv.Appendf(
            TEXT("%d,%.9g,%d,%d,%d,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n"),
            frame,
            frame * optimizedPoses.secondsPerFrame,
            sourceActiveRelationships,
            initialTargetOnlyRelationships,
            optimizedTargetOnlyRelationships,
            optimizedActiveRelationships,
            activeSameSourceBone,
            activeSameTargetBone,
            frameInitialDistanceLoss,
            frameOptimizedDistanceLoss,
            frameInitialDirectionLoss,
            frameOptimizedDirectionLoss,
            frameInitialPenetrationLoss,
            frameOptimizedPenetrationLoss);
    }

    relationshipAggregates.Sort(
        [](const FRelationshipDiagnosticAggregate& left,
           const FRelationshipDiagnosticAggregate& right)
        {
            return left.optimizedDistanceLoss > right.optimizedDistanceLoss;
        });
    FString relationshipSummaryCsv = TEXT("rank,index,firstPoint,secondPoint,activeFrames,sameSourceBone,sameTargetBone,initialDistanceLoss,optimizedDistanceLoss,initialDirectionLoss,optimizedDirectionLoss,initialPenetrationLoss,optimizedPenetrationLoss\n");
    for (int32 rank = 0; rank < relationshipAggregates.Num(); ++rank)
    {
        const FRelationshipDiagnosticAggregate& aggregate =
            relationshipAggregates[rank];
        const FContextDistanceRelationship& relationship =
            configuration.distanceRelationships[aggregate.relationshipIndex];
        relationshipSummaryCsv.Appendf(
            TEXT("%d,%d,%s,%s,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n"),
            rank + 1,
            aggregate.relationshipIndex,
            *relationship.firstPoint.ToString(),
            *relationship.secondPoint.ToString(),
            aggregate.activeFrames,
            aggregate.sameSourceBone ? 1 : 0,
            aggregate.sameTargetBone ? 1 : 0,
            aggregate.initialDistanceLoss,
            aggregate.optimizedDistanceLoss,
            aggregate.initialDirectionLoss,
            aggregate.optimizedDirectionLoss,
            aggregate.initialPenetrationLoss,
            aggregate.optimizedPenetrationLoss);
    }

    FString jacobianCsv = TEXT("frame,timeSeconds,degree,bone,axis,point,dx,dy,dz,magnitude\n");
    constexpr double perturbation = 1.0e-4;
    for (int32 frame = startFrame; frame <= endFrame; ++frame)
    {
        for (std::size_t degree = 0;
             degree < definition.boneRotationDegreesOfFreedom.size();
             ++degree)
        {
            const context::BoneRotationDegreeOfFreedom& degreeOfFreedom =
                definition.boneRotationDegreesOfFreedom[degree];
            context::LocalPose perturbedPose = optimizedPoses.frames[frame];
            context::Quaternion& rotation =
                perturbedPose.transforms[degreeOfFreedom.bone].rotation;
            rotation = (rotation * context::Quaternion::fromAxisAngle(
                degreeOfFreedom.localAxis,
                perturbation)).normalized();
            const std::vector<context::Vector3> perturbedPoints =
                evaluateDiagnosticFramePoints(
                    definition.targetCharacter,
                    perturbedPose);
            const TCHAR* axis = degreeOfFreedom.localAxis.x != 0.0
                ? TEXT("X")
                : degreeOfFreedom.localAxis.y != 0.0
                    ? TEXT("Y")
                    : TEXT("Z");
            for (int32 point = 0; point < configuration.contextPoints.Num(); ++point)
            {
                const context::Vector3 derivative =
                    (perturbedPoints[point] - optimizedPoints[frame][point]) /
                    perturbation;
                jacobianCsv.Appendf(
                    TEXT("%d,%.9g,%llu,%s,%s,%s,%.9g,%.9g,%.9g,%.9g\n"),
                    frame,
                    frame * optimizedPoses.secondsPerFrame,
                    static_cast<uint64>(degree),
                    *targetSkeleton.GetBoneName(degreeOfFreedom.bone).ToString(),
                    axis,
                    *configuration.contextPoints[point].pointName.ToString(),
                    derivative.x,
                    derivative.y,
                    derivative.z,
                    derivative.length());
            }
        }
    }

    const auto saveCsv = [&outputDirectory, &errorMessage](
                             const TCHAR* fileName,
                             const FString& contents)
    {
        const FString path = FPaths::Combine(outputDirectory, fileName);
        if (!FFileHelper::SaveStringToFile(
                contents,
                *path,
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            return setError(
                errorMessage,
                FString::Printf(TEXT("Could not write diagnostic file '%s'."), *path));
        }
        return true;
    };
    return saveCsv(TEXT("bones.csv"), bonesCsv) &&
        saveCsv(TEXT("points.csv"), pointsCsv) &&
        saveCsv(TEXT("frames.csv"), framesCsv) &&
        saveCsv(TEXT("relationships.csv"), relationshipsCsv) &&
        saveCsv(TEXT("relationship_summary.csv"), relationshipSummaryCsv) &&
        saveCsv(TEXT("jacobian.csv"), jacobianCsv);
}
}

bool UContextOptimizationEditorLibrary::validateContextBake(
    UAnimSequence* sourceAnimation,
    UAnimSequence* initialTargetAnimation,
    UContextRetargetConfiguration* configuration,
    FString& errorMessage)
{
    errorMessage.Reset();
    if (!IsValid(configuration))
    {
        return setError(errorMessage, TEXT("No context retarget configuration was provided."));
    }
    if (!validateMeshAndAnimation(
            configuration->sourceSkeletalMesh,
            sourceAnimation,
            TEXT("source"),
            errorMessage) ||
        !validateMeshAndAnimation(
            configuration->targetSkeletalMesh,
            initialTargetAnimation,
            TEXT("target"),
            errorMessage))
    {
        return false;
    }

    const IAnimationDataModel* sourceModel = sourceAnimation->GetDataModel();
    const IAnimationDataModel* targetModel = initialTargetAnimation->GetDataModel();
    if (sourceModel->GetNumberOfKeys() != targetModel->GetNumberOfKeys())
    {
        return setError(
            errorMessage,
            TEXT("Source and initial target animations must have the same number of sampled keys."));
    }
    if (sourceModel->GetNumberOfKeys() <= 0)
    {
        return setError(errorMessage, TEXT("Animations must contain at least one sampled key."));
    }
    if (sourceModel->GetFrameRate() != targetModel->GetFrameRate())
    {
        return setError(
            errorMessage,
            TEXT("Source and initial target animations must have the same sample frame rate."));
    }
    if (configuration->contextPoints.IsEmpty())
    {
        return setError(errorMessage, TEXT("The configuration has no context point pairs."));
    }
    if (configuration->distanceRelationships.IsEmpty())
    {
        return setError(errorMessage, TEXT("The configuration has no point relationships."));
    }
    if (configuration->boneRotationDegreesOfFreedom.IsEmpty())
    {
        return setError(
            errorMessage,
            TEXT("The configuration has no target bone rotation degrees of freedom."));
    }

    const FReferenceSkeleton& sourceSkeleton =
        configuration->sourceSkeletalMesh->GetRefSkeleton();
    const FReferenceSkeleton& targetSkeleton =
        configuration->targetSkeletalMesh->GetRefSkeleton();
    TSet<FName> pointNames;
    for (const FContextPointPair& point : configuration->contextPoints)
    {
        if (point.pointName.IsNone() || pointNames.Contains(point.pointName))
        {
            return setError(
                errorMessage,
                TEXT("Every context point pair must have a unique, non-empty point name."));
        }
        pointNames.Add(point.pointName);
        if (!validateBone(
                sourceSkeleton,
                point.source.boneName,
                FString::Printf(TEXT("Source point '%s'"), *point.pointName.ToString()),
                errorMessage) ||
            !validateBone(
                targetSkeleton,
                point.target.boneName,
                FString::Printf(TEXT("Target point '%s'"), *point.pointName.ToString()),
                errorMessage))
        {
            return false;
        }
        if (point.source.localNormal.ContainsNaN() ||
            point.source.localNormal.IsNearlyZero() ||
            point.target.localNormal.ContainsNaN() ||
            point.target.localNormal.IsNearlyZero())
        {
            return setError(
                errorMessage,
                FString::Printf(
                    TEXT("Context point '%s' has an invalid normal."),
                    *point.pointName.ToString()));
        }
    }

    for (int32 relationshipIndex = 0;
         relationshipIndex < configuration->distanceRelationships.Num();
         ++relationshipIndex)
    {
        const FContextDistanceRelationship& relationship =
            configuration->distanceRelationships[relationshipIndex];
        if (!pointNames.Contains(relationship.firstPoint) ||
            !pointNames.Contains(relationship.secondPoint))
        {
            return setError(
                errorMessage,
                FString::Printf(
                    TEXT("Point relationship at zero-based index %d references a missing context point ('%s' -> '%s')."),
                    relationshipIndex,
                    *relationship.firstPoint.ToString(),
                    *relationship.secondPoint.ToString()));
        }
        if (relationship.firstPoint == relationship.secondPoint)
        {
            return setError(
                errorMessage,
                TEXT("A point relationship must reference two different context points."));
        }
        if (!FMath::IsFinite(relationship.weight) || relationship.weight < 0.0)
        {
            return setError(
                errorMessage,
                TEXT("Point relationship weights must be finite and non-negative."));
        }
        if (!relationship.useDistance &&
            !relationship.useDirection &&
            !relationship.usePenetration)
        {
            return setError(
                errorMessage,
                TEXT("Every relationship must enable at least one descriptor."));
        }
    }

    for (const FBoneRotationDegreeOfFreedom& degreeOfFreedom :
         configuration->boneRotationDegreesOfFreedom)
    {
        if (!validateBone(
                targetSkeleton,
                degreeOfFreedom.boneName,
                TEXT("A target bone rotation degree of freedom"),
                errorMessage))
        {
            return false;
        }
        if (!degreeOfFreedom.x && !degreeOfFreedom.y && !degreeOfFreedom.z)
        {
            return setError(
                errorMessage,
                TEXT("Every target bone entry must enable at least one rotation axis."));
        }
    }

    const FContextOptimizationSettings& settings = configuration->solverSettings;
    if (!FMath::IsFinite(settings.distanceWeight) || settings.distanceWeight < 0.0 ||
        !FMath::IsFinite(settings.directionWeight) || settings.directionWeight < 0.0 ||
        !FMath::IsFinite(settings.penetrationWeight) ||
        settings.penetrationWeight < 0.0 ||
        !FMath::IsFinite(settings.heightWeight) || settings.heightWeight < 0.0 ||
        !FMath::IsFinite(settings.pointPositionRegularizationWeight) ||
        settings.pointPositionRegularizationWeight < 0.0 ||
        !FMath::IsFinite(settings.pointJerkWeight) || settings.pointJerkWeight < 0.0 ||
        !FMath::IsFinite(settings.absoluteGradientStep) ||
        settings.absoluteGradientStep < 0.0 ||
        !FMath::IsFinite(settings.relativeGradientStep) ||
        settings.relativeGradientStep < 0.0 ||
        (settings.absoluteGradientStep == 0.0 && settings.relativeGradientStep == 0.0))
    {
        return setError(
            errorMessage,
            TEXT("Loss weights and numerical-gradient steps are invalid."));
    }
    if (settings.useTemporalSmoothing &&
        (!FMath::IsFinite(settings.temporalWindowDurationSeconds) ||
         settings.temporalWindowDurationSeconds <= 0.0 ||
         !FMath::IsFinite(settings.temporalWindowOverlapSeconds) ||
         settings.temporalWindowOverlapSeconds < 0.0 ||
         settings.temporalWindowOverlapSeconds >=
             settings.temporalWindowDurationSeconds ||
         sourceModel->GetNumberOfKeys() < 4))
    {
        return setError(
            errorMessage,
            TEXT("Temporal smoothing needs a positive window duration, a smaller non-negative overlap, and at least four animation keys."));
    }
    if (settings.worldUp.ContainsNaN() || settings.worldUp.IsNearlyZero() ||
        !FMath::IsFinite(settings.sourceGroundHeight) ||
        !FMath::IsFinite(settings.targetGroundHeight) ||
        !FMath::IsFinite(settings.interactionMinimumDistanceRatio) ||
        !FMath::IsFinite(settings.interactionMaximumDistanceRatio) ||
        !FMath::IsFinite(settings.floorMinimumHeightRatio) ||
        !FMath::IsFinite(settings.floorMaximumHeightRatio) ||
        settings.interactionMinimumDistanceRatio < 0.0 ||
        settings.interactionMaximumDistanceRatio <=
            settings.interactionMinimumDistanceRatio ||
        settings.floorMinimumHeightRatio < 0.0 ||
        settings.floorMaximumHeightRatio <= settings.floorMinimumHeightRatio)
    {
        return setError(
            errorMessage,
            TEXT("Environment or adaptive-weight settings are invalid."));
    }
    if (!FMath::IsFinite(settings.learningRate) || settings.learningRate <= 0.0 ||
        !FMath::IsFinite(settings.beta1) || settings.beta1 < 0.0 || settings.beta1 >= 1.0 ||
        !FMath::IsFinite(settings.beta2) || settings.beta2 < 0.0 || settings.beta2 >= 1.0 ||
        !FMath::IsFinite(settings.epsilon) || settings.epsilon <= 0.0)
    {
        return setError(errorMessage, TEXT("Adam settings are invalid."));
    }
    if (settings.maxIterations < 1 ||
        !FMath::IsFinite(settings.gradientTolerance) ||
        settings.gradientTolerance < 0.0 ||
        !FMath::IsFinite(settings.minimumRelativeLossImprovement) ||
        settings.minimumRelativeLossImprovement < 0.0 ||
        settings.lossImprovementPatience < 1 ||
        settings.iterationLogInterval < 1)
    {
        return setError(errorMessage, TEXT("Optimization settings are invalid."));
    }

    return true;
}

FContextBakeEstimate UContextOptimizationEditorLibrary::estimateContextBake(
    UAnimSequence* initialTargetAnimation,
    UContextRetargetConfiguration* configuration)
{
    FContextBakeEstimate result;
    if (!IsValid(initialTargetAnimation) ||
        !initialTargetAnimation->IsDataModelValid() ||
        !IsValid(configuration))
    {
        return result;
    }

    result.frameCount = initialTargetAnimation->GetDataModel()->GetNumberOfKeys();
    for (const FBoneRotationDegreeOfFreedom& degreeOfFreedom :
         configuration->boneRotationDegreesOfFreedom)
    {
        result.degreesOfFreedomPerFrame += degreeOfFreedom.x ? 1 : 0;
        result.degreesOfFreedomPerFrame += degreeOfFreedom.y ? 1 : 0;
        result.degreesOfFreedomPerFrame += degreeOfFreedom.z ? 1 : 0;
    }
    result.parameterCount =
        static_cast<int64>(result.frameCount) * result.degreesOfFreedomPerFrame;
    const bool useTemporalSmoothing =
        configuration->solverSettings.useTemporalSmoothing &&
        configuration->solverSettings.pointJerkWeight > 0.0;
    const double secondsPerFrame =
        initialTargetAnimation->GetDataModel()->GetFrameRate().AsInterval();
    const int32 framesPerSolve = useTemporalSmoothing
        ? FMath::Min(
              result.frameCount,
              FMath::Max(
                  4,
                  FMath::RoundToInt(
                      configuration->solverSettings.temporalWindowDurationSeconds /
                      secondsPerFrame)))
        : 1;
    const int32 overlapFrames = useTemporalSmoothing
        ? FMath::Clamp(
              FMath::RoundToInt(
                  configuration->solverSettings.temporalWindowOverlapSeconds /
                  secondsPerFrame),
              0,
              framesPerSolve - 1)
        : 0;
    const TArray<FTemporalWindow> windows = createTemporalWindows(
        result.frameCount,
        framesPerSolve,
        overlapFrames);
    result.windowCount = windows.Num();
    result.lossEvaluationsPerWindowIteration =
        2 * framesPerSolve * result.degreesOfFreedomPerFrame + 1;
    result.lossEvaluationsPerFrameIteration =
        result.lossEvaluationsPerWindowIteration;
    for (const FTemporalWindow& window : windows)
    {
        result.lossEvaluationsPerIteration +=
            2 * static_cast<int64>(window.frameCount) *
                result.degreesOfFreedomPerFrame +
            1;
    }
    return result;
}

FContextBakeResult UContextOptimizationEditorLibrary::bakeContextOptimizedAnimation(
    UAnimSequence* sourceAnimation,
    UAnimSequence* initialTargetAnimation,
    UContextRetargetConfiguration* configuration,
    const FString& outputPackagePath,
    const FString& outputAssetName)
{
    FContextBakeResult bakeResult;
    if (!validateContextBake(
            sourceAnimation,
            initialTargetAnimation,
            configuration,
            bakeResult.errorMessage))
    {
        return bakeResult;
    }
    if (!validateOutputTarget(
            outputPackagePath,
            outputAssetName,
            bakeResult.errorMessage))
    {
        return bakeResult;
    }

    const int32 frameCount = initialTargetAnimation->GetDataModel()->GetNumberOfKeys();
    const FContextOptimizationSettings& settings = configuration->solverSettings;
    const bool useTemporalSmoothing =
        settings.useTemporalSmoothing && settings.pointJerkWeight > 0.0;
    const double secondsPerFrame =
        initialTargetAnimation->GetDataModel()->GetFrameRate().AsInterval();
    const int32 windowFrameCount = useTemporalSmoothing
        ? FMath::Min(
              frameCount,
              FMath::Max(
                  4,
                  FMath::RoundToInt(
                      settings.temporalWindowDurationSeconds / secondsPerFrame)))
        : 1;
    const int32 overlapFrameCount = useTemporalSmoothing
        ? FMath::Clamp(
              FMath::RoundToInt(
                  settings.temporalWindowOverlapSeconds / secondsPerFrame),
              0,
              windowFrameCount - 1)
        : 0;
    const int32 solveCount = createTemporalWindows(
        frameCount,
        windowFrameCount,
        overlapFrameCount).Num();
    FScopedSlowTask slowTask(
        static_cast<float>(solveCount + 2),
        LOCTEXT("ContextBakeProgress", "Baking context-optimized animation"));
    slowTask.MakeDialog(true);
    slowTask.EnterProgressFrame(1.0f, LOCTEXT("BuildInput", "Building solver input"));

    try
    {
        const context::ContextDefinition definition =
            createContextDefinition(*configuration);
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Source character height %.3f m; adaptive interaction range %.3f to %.3f m; adaptive floor range %.3f to %.3f m"),
            definition.sourceCharacter.height,
            definition.sourceCharacter.height *
                definition.adaptiveWeights.interactionMinimumDistanceRatio,
            definition.sourceCharacter.height *
                definition.adaptiveWeights.interactionMaximumDistanceRatio,
            definition.sourceCharacter.height *
                definition.adaptiveWeights.floorMinimumHeightRatio,
            definition.sourceCharacter.height *
                definition.adaptiveWeights.floorMaximumHeightRatio);
        TArray<TArray<FVector>> targetScales;
        context::ContextBatchInput input;
        input.sourcePoses = createPoseBatch(
            *configuration->sourceSkeletalMesh,
            *sourceAnimation,
            nullptr);
        input.initialTargetPoses = createPoseBatch(
            *configuration->targetSkeletalMesh,
            *initialTargetAnimation,
            &targetScales);

        const context::ContextSolverSettings solverSettings =
            createSolverSettings(configuration->solverSettings);
        FFrameSolveSummary solveSummary;
        if (!solveFrames(
            definition,
            input,
            solverSettings,
            settings.temporalWindowDurationSeconds,
            settings.temporalWindowOverlapSeconds,
            settings.iterationLogInterval,
            slowTask,
            solveSummary))
        {
            bakeResult.errorMessage = TEXT("Context optimization was cancelled.");
            return bakeResult;
        }

        slowTask.EnterProgressFrame(1.0f, LOCTEXT("WriteOutput", "Writing animation asset"));
        const FString packageName = makePackageName(outputPackagePath, outputAssetName);
        UAnimSequence* outputAnimation = duplicateTargetAnimation(
            *initialTargetAnimation,
            packageName,
            outputAssetName,
            bakeResult.errorMessage);
        if (!IsValid(outputAnimation))
        {
            return bakeResult;
        }
        if (!writeSolvedTracks(
                *outputAnimation,
                *configuration->targetSkeletalMesh,
                solveSummary.targetPoses,
                targetScales,
                bakeResult.errorMessage))
        {
            outputAnimation->ClearFlags(RF_Public | RF_Standalone);
            return bakeResult;
        }

        outputAnimation->MarkPackageDirty();
        outputAnimation->PostEditChange();
        FAssetRegistryModule::AssetCreated(outputAnimation);
        bakeResult.success = true;
        bakeResult.animation = outputAnimation;
        bakeResult.initialLoss = solveSummary.initialLoss.weightedTotal(
            solverSettings.lossWeights);
        bakeResult.finalLoss = solveSummary.finalLoss.weightedTotal(
            solverSettings.lossWeights);
        bakeResult.iterations = solveSummary.maximumIterationsPerWindow;
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Context bake loss %.9g -> %.9g | distance %.9g -> %.9g | direction %.9g -> %.9g | penetration %.9g -> %.9g | height %.9g -> %.9g | regularization %.9g -> %.9g | point jerk %.9g -> %.9g"),
            bakeResult.initialLoss,
            bakeResult.finalLoss,
            solveSummary.initialLoss.distance,
            solveSummary.finalLoss.distance,
            solveSummary.initialLoss.direction,
            solveSummary.finalLoss.direction,
            solveSummary.initialLoss.penetration,
            solveSummary.finalLoss.penetration,
            solveSummary.initialLoss.height,
            solveSummary.finalLoss.height,
            solveSummary.initialLoss.pointPositionRegularization,
            solveSummary.finalLoss.pointPositionRegularization,
            solveSummary.initialLoss.pointJerk,
            solveSummary.finalLoss.pointJerk);
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Context bake weighted loss components | distance %.9g -> %.9g | direction %.9g -> %.9g | penetration %.9g -> %.9g | height %.9g -> %.9g | regularization %.9g -> %.9g | point jerk %.9g -> %.9g"),
            solverSettings.lossWeights.distance * solveSummary.initialLoss.distance,
            solverSettings.lossWeights.distance * solveSummary.finalLoss.distance,
            solverSettings.lossWeights.direction * solveSummary.initialLoss.direction,
            solverSettings.lossWeights.direction * solveSummary.finalLoss.direction,
            solverSettings.lossWeights.penetration * solveSummary.initialLoss.penetration,
            solverSettings.lossWeights.penetration * solveSummary.finalLoss.penetration,
            solverSettings.lossWeights.height * solveSummary.initialLoss.height,
            solverSettings.lossWeights.height * solveSummary.finalLoss.height,
            solverSettings.lossWeights.pointPositionRegularization *
                solveSummary.initialLoss.pointPositionRegularization,
            solverSettings.lossWeights.pointPositionRegularization *
                solveSummary.finalLoss.pointPositionRegularization,
            solverSettings.lossWeights.pointJerk * solveSummary.initialLoss.pointJerk,
            solverSettings.lossWeights.pointJerk * solveSummary.finalLoss.pointJerk);
        UE_LOG(
            LogContextOptimization,
            Display,
            TEXT("Context bake weighted gradient L2 magnitudes | total %.9g -> %.9g | distance %.9g -> %.9g | direction %.9g -> %.9g | penetration %.9g -> %.9g | height %.9g -> %.9g | regularization %.9g -> %.9g | point jerk %.9g -> %.9g"),
            solveSummary.initialGradientMagnitudes.total,
            solveSummary.finalGradientMagnitudes.total,
            solveSummary.initialGradientMagnitudes.distance,
            solveSummary.finalGradientMagnitudes.distance,
            solveSummary.initialGradientMagnitudes.direction,
            solveSummary.finalGradientMagnitudes.direction,
            solveSummary.initialGradientMagnitudes.penetration,
            solveSummary.finalGradientMagnitudes.penetration,
            solveSummary.initialGradientMagnitudes.height,
            solveSummary.finalGradientMagnitudes.height,
            solveSummary.initialGradientMagnitudes.pointPositionRegularization,
            solveSummary.finalGradientMagnitudes.pointPositionRegularization,
            solveSummary.initialGradientMagnitudes.pointJerk,
            solveSummary.finalGradientMagnitudes.pointJerk);
    }
    catch (const std::exception& exception)
    {
        bakeResult.errorMessage = UTF8_TO_TCHAR(exception.what());
    }

    return bakeResult;
}

UContextOptimizationDiagnosticsCommandlet::UContextOptimizationDiagnosticsCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UContextOptimizationDiagnosticsCommandlet::Main(const FString& parameters)
{
    FString sourcePath;
    FString initialTargetPath;
    FString optimizedPath;
    FString configurationPath;
    FString outputDirectory;
    int32 startFrame = 0;
    int32 endFrame = TNumericLimits<int32>::Max();
    FParse::Value(*parameters, TEXT("Source="), sourcePath);
    FParse::Value(*parameters, TEXT("InitialTarget="), initialTargetPath);
    FParse::Value(*parameters, TEXT("Optimized="), optimizedPath);
    FParse::Value(*parameters, TEXT("Configuration="), configurationPath);
    FParse::Value(*parameters, TEXT("Output="), outputDirectory);
    FParse::Value(*parameters, TEXT("StartFrame="), startFrame);
    FParse::Value(*parameters, TEXT("EndFrame="), endFrame);
    const bool runGradientAudit = FParse::Param(
        *parameters,
        TEXT("GradientAudit"));
    if (outputDirectory.IsEmpty())
    {
        outputDirectory = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("ContextOptimization"),
            TEXT("Diagnostics"));
    }

    UAnimSequence* sourceAnimation = LoadObject<UAnimSequence>(nullptr, *sourcePath);
    UAnimSequence* initialTargetAnimation =
        LoadObject<UAnimSequence>(nullptr, *initialTargetPath);
    UAnimSequence* optimizedAnimation =
        LoadObject<UAnimSequence>(nullptr, *optimizedPath);
    UContextRetargetConfiguration* configuration =
        LoadObject<UContextRetargetConfiguration>(nullptr, *configurationPath);
    if (!IsValid(sourceAnimation) ||
        !IsValid(initialTargetAnimation) ||
        !IsValid(optimizedAnimation) ||
        !IsValid(configuration))
    {
        UE_LOG(
            LogContextOptimization,
            Error,
            TEXT("Could not load one or more diagnostic assets. Source='%s' InitialTarget='%s' Optimized='%s' Configuration='%s'"),
            *sourcePath,
            *initialTargetPath,
            *optimizedPath,
            *configurationPath);
        return 1;
    }

    FString errorMessage;
    if (!writeAnimationDiagnostics(
            *sourceAnimation,
            *initialTargetAnimation,
            *optimizedAnimation,
            *configuration,
            startFrame,
            endFrame,
            outputDirectory,
            errorMessage))
    {
        UE_LOG(LogContextOptimization, Error, TEXT("%s"), *errorMessage);
        return 1;
    }
    if (runGradientAudit && !writeGradientAudit(
            *sourceAnimation,
            *initialTargetAnimation,
            *optimizedAnimation,
            *configuration,
            startFrame,
            endFrame,
            outputDirectory,
            errorMessage))
    {
        UE_LOG(LogContextOptimization, Error, TEXT("%s"), *errorMessage);
        return 1;
    }
    UE_LOG(
        LogContextOptimization,
        Display,
        TEXT("Wrote context optimization diagnostics to '%s'."),
        *outputDirectory);
    return 0;
}

#undef LOCTEXT_NAMESPACE
