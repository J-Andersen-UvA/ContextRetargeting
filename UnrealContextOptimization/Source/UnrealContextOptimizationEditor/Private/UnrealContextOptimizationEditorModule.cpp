#include "ContextPointAuthoringActor.h"
#include "ContextPointAuthoringActorDetails.h"
#include "ContextRetargetConfiguration.h"
#include "ContextRetargetConfigurationDetails.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

class FUnrealContextOptimizationEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FPropertyEditorModule& propertyEditor =
            FModuleManager::LoadModuleChecked<FPropertyEditorModule>(
                TEXT("PropertyEditor"));
        propertyEditor.RegisterCustomClassLayout(
            AContextPointAuthoringActor::StaticClass()->GetFName(),
            FOnGetDetailCustomizationInstance::CreateStatic(
                &FContextPointAuthoringActorDetails::makeInstance));
        propertyEditor.RegisterCustomClassLayout(
            UContextRetargetConfiguration::StaticClass()->GetFName(),
            FOnGetDetailCustomizationInstance::CreateStatic(
                &FContextRetargetConfigurationDetails::makeInstance));
        propertyEditor.NotifyCustomizationModuleChanged();
    }

    virtual void ShutdownModule() override
    {
        if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
        {
            FPropertyEditorModule& propertyEditor =
                FModuleManager::GetModuleChecked<FPropertyEditorModule>(
                    TEXT("PropertyEditor"));
            propertyEditor.UnregisterCustomClassLayout(
                AContextPointAuthoringActor::StaticClass()->GetFName());
            propertyEditor.UnregisterCustomClassLayout(
                UContextRetargetConfiguration::StaticClass()->GetFName());
            propertyEditor.NotifyCustomizationModuleChanged();
        }
    }
};

IMPLEMENT_MODULE(
    FUnrealContextOptimizationEditorModule,
    UnrealContextOptimizationEditor)
