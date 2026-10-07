#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ContextRetargetConfiguration.generated.h"

class USkeletalMesh;

USTRUCT(BlueprintType)
struct UNREALCONTEXTOPTIMIZATION_API FContextPointAttachment
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FName boneName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FVector localPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FVector localNormal = FVector::ForwardVector;
};

USTRUCT(BlueprintType)
struct UNREALCONTEXTOPTIMIZATION_API FContextPointPair
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FName pointName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FContextPointAttachment source;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FContextPointAttachment target;
};

USTRUCT(BlueprintType)
struct UNREALCONTEXTOPTIMIZATION_API FContextDistanceRelationship
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relationship")
    FName firstPoint;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relationship")
    FName secondPoint;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relationship", meta = (ClampMin = "0.0"))
    double weight = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relationship")
    bool useDistance = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relationship")
    bool useDirection = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relationship")
    bool usePenetration = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relationship")
    bool useAdaptiveWeight = true;
};

USTRUCT(BlueprintType)
struct UNREALCONTEXTOPTIMIZATION_API FBoneRotationDegreeOfFreedom
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Degree Of Freedom")
    FName boneName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Degree Of Freedom", meta = (DisplayName = "X"))
    bool x = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Degree Of Freedom", meta = (DisplayName = "Y"))
    bool y = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Degree Of Freedom", meta = (DisplayName = "Z"))
    bool z = true;
};

USTRUCT(BlueprintType)
struct UNREALCONTEXTOPTIMIZATION_API FContextOptimizationSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loss", meta = (ClampMin = "0.0"))
    double distanceWeight = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loss", meta = (ClampMin = "0.0"))
    double directionWeight = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loss", meta = (ClampMin = "0.0"))
    double penetrationWeight = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loss", meta = (ClampMin = "0.0"))
    double heightWeight = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loss", meta = (ClampMin = "0.0"))
    double pointPositionRegularizationWeight = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing")
    bool useTemporalSmoothing = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.0", EditCondition = "useTemporalSmoothing"))
    double pointJerkWeight = 0.1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.1", EditCondition = "useTemporalSmoothing"))
    double temporalWindowDurationSeconds = 3.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing", meta = (ClampMin = "0.0", EditCondition = "useTemporalSmoothing"))
    double temporalWindowOverlapSeconds = 0.25;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weights")
    bool useAdaptiveWeights = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weights", meta = (EditCondition = "useAdaptiveWeights"))
    bool useTargetAdaptiveWeights = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weights", meta = (ClampMin = "0.0"))
    double interactionMinimumDistanceRatio = 0.05;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weights", meta = (ClampMin = "0.0"))
    double interactionMaximumDistanceRatio = 0.15;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weights", meta = (ClampMin = "0.0"))
    double floorMinimumHeightRatio = 0.05;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptive Weights", meta = (ClampMin = "0.0"))
    double floorMaximumHeightRatio = 0.15;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
    FVector worldUp = FVector::UpVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
    double sourceGroundHeight = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
    double targetGroundHeight = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Numerical Gradient", meta = (ClampMin = "0.0"))
    double absoluteGradientStep = 1.0e-6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Numerical Gradient", meta = (ClampMin = "0.0"))
    double relativeGradientStep = 1.0e-6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adam", meta = (ClampMin = "0.0"))
    double learningRate = 1.0e-3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adam", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    double beta1 = 0.9;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adam", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    double beta2 = 0.999;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adam", meta = (ClampMin = "0.0"))
    double epsilon = 1.0e-8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization", meta = (ClampMin = "1"))
    int32 maxIterations = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization", meta = (ClampMin = "0.0"))
    double gradientTolerance = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization", meta = (ClampMin = "0.0"))
    double minimumRelativeLossImprovement = 0.0001;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization", meta = (ClampMin = "1"))
    int32 lossImprovementPatience = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization", meta = (ClampMin = "1"))
    int32 iterationLogInterval = 10;
};

UCLASS(BlueprintType)
class UNREALCONTEXTOPTIMIZATION_API UContextRetargetConfiguration : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Characters")
    TObjectPtr<USkeletalMesh> sourceSkeletalMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Characters")
    TObjectPtr<USkeletalMesh> targetSkeletalMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Context")
    TArray<FContextPointPair> contextPoints;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Configuration Tools")
    FName pointNameToRemove;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context", meta = (DisplayName = "Point Relationships"))
    TArray<FContextDistanceRelationship> distanceRelationships;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
    TArray<FBoneRotationDegreeOfFreedom> boneRotationDegreesOfFreedom;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solver")
    FContextOptimizationSettings solverSettings;
};
