#include "ContextPointComponent.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

UContextPointComponent::UContextPointComponent()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> sphereMesh(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (sphereMesh.Succeeded())
    {
        SetStaticMesh(sphereMesh.Object);
    }

    SetRelativeScale3D(FVector(0.025));
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCastShadow(false);
    SetHiddenInGame(true);
    SetMobility(EComponentMobility::Movable);
}

void UContextPointComponent::OnRegister()
{
    Super::OnRegister();

    SetRelativeScale3D(FVector(0.025));

    if (!IsValid(pointMaterial))
    {
        UMaterialInterface* baseMaterial = LoadObject<UMaterialInterface>(
            nullptr,
            TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial"));
        if (baseMaterial != nullptr)
        {
            pointMaterial = UMaterialInstanceDynamic::Create(baseMaterial, this);
            const FLinearColor calmYellow =
                FLinearColor::FromSRGBColor(FColor(244, 210, 96));
            pointMaterial->SetVectorParameterValue(
                TEXT("Color"),
                calmYellow * 1.15f);
        }
    }

    if (pointMaterial != nullptr)
    {
        SetMaterial(0, pointMaterial);
    }
}
