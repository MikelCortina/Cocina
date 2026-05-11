#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PJR_GameInstance.generated.h"

UCLASS()
class COCINA_API UPJR_GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Session")
	FString PlayerName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Session")
	FString RoomName;

	UFUNCTION(BlueprintCallable, Category = "Session")
	void SetSessionData(const FString& NewPlayerName, const FString& NewRoomName);

	UFUNCTION(BlueprintCallable, Category = "Session")
	FString GetPlayerName() const;

	UFUNCTION(BlueprintCallable, Category = "Session")
	FString GetRoomName() const;
};