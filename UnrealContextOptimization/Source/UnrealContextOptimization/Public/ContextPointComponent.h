#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "ContextPointComponent.generated.h"

UCLASS(ClassGroup = (ContextRetargeting), meta = (BlueprintSpawnableComponent))
class UNREALCONTEXTOPTIMIZATION_API UContextPointComponent : public UStaticMeshComponent
{
    GENERATED_BODY()

public:
    UContextPointComponent();

    virtual void OnRegister() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FName pointName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Point")
    FName boneName;

private:
    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInstanceDynamic> pointMaterial;
};
