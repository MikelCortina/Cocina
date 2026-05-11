// Copyright Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TickableEditorObject.h"
#include "Engine/World.h"

class UFusionClient;

struct FBandwidthSample
{
	size_t Bytes = 0;
};

struct FPropertyBandwidthData
{
	FString Name;
	int32 ChangedWordCount = 0;
	int32 WordCount = 0;
};

struct FSubObjectBandwidthData
{
	uint64 PackedId = 0;
	uint32 Origin = 0;
	uint32 Counter = 0;
	FString Label;
	FLinearColor GraphColor = FLinearColor::White;
	TArray<FBandwidthSample> History;
	int32 HistoryHead = 0;
	size_t HeldBytes = 0;
	int32 HoldFrames = 0;
	TArray<FPropertyBandwidthData> Properties;
	bool bIsSelected = false;
	bool bIsAlive = true;

	void PushSample(size_t Bytes)
	{
		if (History.Num() < MaxHistory)
		{
			History.Add({Bytes});
			HistoryHead = History.Num();
		}
		else
		{
			History[HistoryHead % MaxHistory] = {Bytes};
			++HistoryHead;
		}
	}

	int32 GetSampleCount() const
	{
		return FMath::Min(History.Num(), MaxHistory);
	}

	FBandwidthSample GetSample(int32 Index) const
	{
		const int32 Count = FMath::Min(History.Num(), MaxHistory);
		if (Count < MaxHistory)
		{
			return History[Index];
		}
		const int32 Actual = (HistoryHead + Index) % MaxHistory;
		return History[Actual];
	}

	static constexpr int32 MaxHistory = 300;
};

struct FObjectBandwidthData
{
	uint64 PackedId = 0;
	uint32 Origin = 0;
	uint32 Counter = 0;
	FString ObjectLabel;
	FLinearColor GraphColor = FLinearColor::White;
	TArray<FBandwidthSample> SendHistory;
	int32 SendHistoryHead = 0;
	TArray<FBandwidthSample> ReceiveHistory;
	int32 ReceiveHistoryHead = 0;
	size_t HeldReceiveBytes = 0;
	int32 ReceiveHoldFrames = 0;
	TMap<uint64, FSubObjectBandwidthData> SubObjects;
	TArray<FPropertyBandwidthData> Properties;
	bool bIsSelected = false;
	bool bIsAlive = true;
	bool bIsSending = false;

	void PushSendSample(size_t Bytes)
	{
		PushTo(SendHistory, SendHistoryHead, Bytes);
	}

	void PushReceiveSample(size_t Bytes)
	{
		PushTo(ReceiveHistory, ReceiveHistoryHead, Bytes);
	}

	int32 GetSendSampleCount() const
	{
		return FMath::Min(SendHistory.Num(), MaxHistory);
	}

	int32 GetReceiveSampleCount() const
	{
		return FMath::Min(ReceiveHistory.Num(), MaxHistory);
	}

	FBandwidthSample GetSendSample(int32 Index) const
	{
		return GetFrom(SendHistory, SendHistoryHead, Index);
	}

	FBandwidthSample GetReceiveSample(int32 Index) const
	{
		return GetFrom(ReceiveHistory, ReceiveHistoryHead, Index);
	}

	static constexpr int32 MaxHistory = 300;

private:
	static void PushTo(TArray<FBandwidthSample>& History, int32& Head, size_t Bytes)
	{
		if (History.Num() < MaxHistory)
		{
			History.Add({Bytes});
			Head = History.Num();
		}
		else
		{
			History[Head % MaxHistory] = {Bytes};
			++Head;
		}
	}

	static FBandwidthSample GetFrom(const TArray<FBandwidthSample>& History, int32 Head, int32 Index)
	{
		const int32 Count = FMath::Min(History.Num(), MaxHistory);
		if (Count < MaxHistory)
		{
			return History[Index];
		}
		const int32 Actual = (Head + Index) % MaxHistory;
		return History[Actual];
	}
};

DECLARE_MULTICAST_DELEGATE(FOnBandwidthDataUpdated);

class FFusionBandwidthDataCollector : public FTickableEditorObject
{
public:
	FFusionBandwidthDataCollector();
	virtual ~FFusionBandwidthDataCollector() override;

	// FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return true; }

	void SetTargetWorld(UWorld* InWorld) { TargetWorld = InWorld; }
	UWorld* GetTargetWorld() const { return TargetWorld.Get(); }

	void SetRecording(bool bInRecording) { bRecording = bInRecording; }
	bool IsRecording() const { return bRecording; }
	void ClearAllData();

	const TMap<uint64, FObjectBandwidthData>& GetObjectData() const { return ObjectData; }
	TMap<uint64, FObjectBandwidthData>& GetObjectDataMutable() { return ObjectData; }
	int32 GetObjectCount() const { return ObjectData.Num(); }
	bool IsConnected() const { return bConnected; }
	uint64 GetTotalBytesSent() const { return TotalBytesSent; }
	uint64 GetTotalBytesReceived() const { return TotalBytesReceived; }

	FOnBandwidthDataUpdated OnDataUpdated;

private:
	UFusionClient* FindActiveFusionClient() const;
	FLinearColor AssignColor();

	TWeakObjectPtr<UWorld> TargetWorld;
	TMap<uint64, FObjectBandwidthData> ObjectData;
	bool bRecording = true;
	bool bConnected = false;
	int32 ColorIndex = 0;
	uint64 TotalBytesSent = 0;
	uint64 TotalBytesReceived = 0;
};