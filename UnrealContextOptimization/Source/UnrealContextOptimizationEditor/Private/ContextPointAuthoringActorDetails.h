#pragma once

#include "IDetailCustomization.h"

class AContextPointAuthoringActor;
class IDetailLayoutBuilder;

class FContextPointAuthoringActorDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> makeInstance();

    virtual void CustomizeDetails(IDetailLayoutBuilder& detailBuilder) override;

private:
    using ActorAction = void (AContextPointAuthoringActor::*)();

    FReply runAction(ActorAction action);
    TSharedRef<SWidget> buildPresetMenu();
    void loadContextPointPreset(FString presetPath);
    void browseForContextPointPreset();
    FReply rotatePointsToSurfaceNormals();
    FReply playPreview();
    FReply stopPreview();

    AContextPointAuthoringActor* findPreviewActor(
        const AContextPointAuthoringActor& templateActor) const;

    TArray<TWeakObjectPtr<AContextPointAuthoringActor>> authoringActors;
};
