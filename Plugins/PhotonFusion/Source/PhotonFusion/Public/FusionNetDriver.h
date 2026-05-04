// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Engine/NetDriver.h"

#include "FusionNetDriver.generated.h"

UCLASS(transient, config = Engine, MinimalAPI)
class UFusionNetDriver : public UNetDriver
{
	GENERATED_BODY()

public:
	virtual bool IsAvailable() const override { return true; }
	virtual bool InitConnect(FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error) override;
	virtual void TickDispatch(float DeltaSeconds) override {}
	virtual void TickFlush(float DeltaSeconds) override {}
	virtual void LowLevelDestroy() override {}
	virtual bool IsServer() const override;
	virtual void Shutdown() override {}
	virtual void Cleanup();

	virtual void CleanPackageMaps() override;

	void SetIsServer(bool bToggle);

	virtual void ProcessRemoteFunction(AActor* Actor, UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack, UObject* SubObject = nullptr ) override;
	virtual bool ShouldReplicateFunction(AActor* Actor, UFunction* Function) const override;

private:
	bool bIsActingAsAServer = false;
	
};
