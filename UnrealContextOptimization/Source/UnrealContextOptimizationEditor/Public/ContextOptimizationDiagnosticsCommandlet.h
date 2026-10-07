#pragma once

#include "Commandlets/Commandlet.h"
#include "ContextOptimizationDiagnosticsCommandlet.generated.h"

UCLASS()
class UNREALCONTEXTOPTIMIZATIONEDITOR_API UContextOptimizationDiagnosticsCommandlet
    : public UCommandlet
{
    GENERATED_BODY()

public:
    UContextOptimizationDiagnosticsCommandlet();

    virtual int32 Main(const FString& parameters) override;
};
