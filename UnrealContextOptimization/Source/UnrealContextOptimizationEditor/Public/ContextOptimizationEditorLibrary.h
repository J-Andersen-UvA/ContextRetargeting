#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ContextOptimizationEditorLibrary.generated.h"

class UAnimSequence;
class UContextRetargetConfiguration;

USTRUCT(BlueprintType)
struct UNREALCONTEXTOPTIMIZATIONEDITOR_API FContextBakeResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    bool success = false;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    TObjectPtr<UAnimSequence> animation;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    FString errorMessage;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    double initialLoss = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    double finalLoss = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int32 iterations = 0;
};

USTRUCT(BlueprintType)
struct UNREALCONTEXTOPTIMIZATIONEDITOR_API FContextBakeEstimate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int32 frameCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int32 degreesOfFreedomPerFrame = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int64 parameterCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int64 lossEvaluationsPerIteration = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int32 windowCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int64 lossEvaluationsPerWindowIteration = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Context Bake")
    int64 lossEvaluationsPerFrameIteration = 0;
};

UCLASS()
class UNREALCONTEXTOPTIMIZATIONEDITOR_API UContextOptimizationEditorLibrary
    : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Context Optimization|Editor")
    static FContextBakeResult bakeContextOptimizedAnimation(
        UAnimSequence* sourceAnimation,
        UAnimSequence* initialTargetAnimation,
        UContextRetargetConfiguration* configuration,
        const FString& outputPackagePath,
        const FString& outputAssetName);

    UFUNCTION(BlueprintPure, Category = "Context Optimization|Editor")
    static bool validateContextBake(
        UAnimSequence* sourceAnimation,
        UAnimSequence* initialTargetAnimation,
        UContextRetargetConfiguration* configuration,
        FString& errorMessage);

    UFUNCTION(BlueprintPure, Category = "Context Optimization|Editor")
    static FContextBakeEstimate estimateContextBake(
        UAnimSequence* initialTargetAnimation,
        UContextRetargetConfiguration* configuration);
};
