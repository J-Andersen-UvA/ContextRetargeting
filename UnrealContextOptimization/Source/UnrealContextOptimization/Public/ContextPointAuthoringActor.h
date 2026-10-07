#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ContextPointAuthoringActor.generated.h"

class UAnimationAsset;
class UContextPointComponent;
class UContextRetargetConfiguration;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EContextAuthoringCharacter : uint8
{
    Source,
    Target
};

UCLASS(Blueprintable)
class UNREALCONTEXTOPTIMIZATION_API AContextPointAuthoringActor : public AActor
{
    GENERATED_BODY()

public:
    AContextPointAuthoringActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring")
    TObjectPtr<USkeletalMeshComponent> skeletalMeshComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Authoring")
    EContextAuthoringCharacter character = EContextAuthoringCharacter::Source;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Authoring")
    TObjectPtr<UContextRetargetConfiguration> configuration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Add Point")
    FName newPointName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Add Point")
    FName newPointBone;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
    TObjectPtr<UAnimationAsset> previewAnimation;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Instanced, Category = "Context Points")
    TArray<TObjectPtr<UContextPointComponent>> contextPoints;

    UFUNCTION(BlueprintCallable, Category = "Context Point Authoring")
    void addContextPoint();

    UFUNCTION(BlueprintCallable, Category = "Context Point Authoring")
    void refreshPointAttachments();

    UFUNCTION(BlueprintCallable, Category = "Context Point Authoring")
    void capturePointsToConfiguration();

    UFUNCTION(BlueprintCallable, Category = "Context Point Authoring")
    void clearContextPoints();

    UFUNCTION(BlueprintCallable, Category = "Context Point Authoring")
    void playPreviewAnimation();

    UFUNCTION(BlueprintCallable, Category = "Context Point Authoring")
    void stopPreviewAnimation();
};
