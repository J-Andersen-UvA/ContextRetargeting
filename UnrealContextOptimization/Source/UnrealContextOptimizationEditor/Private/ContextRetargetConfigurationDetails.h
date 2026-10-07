#pragma once

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IPropertyUtilities;
class UContextRetargetConfiguration;

class FContextRetargetConfigurationDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> makeInstance();

    virtual void CustomizeDetails(IDetailLayoutBuilder& detailBuilder) override;

private:
    FReply removePoint();
    FReply exportJson();
    TSharedRef<SWidget> buildRelationshipPresetMenu();
    void importRelationships(FString presetPath);
    void browseForRelationshipPreset();

    TArray<TWeakObjectPtr<UContextRetargetConfiguration>> configurations;
    TWeakPtr<IPropertyUtilities> propertyUtilities;
};
