// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Logging/FusionOnScreenDebugMessageLogOutput.h"
#include "CoreMinimal.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "FusionShared.h"

void FusionOnScreenDebugMessageLogOutput::LogTrace(const PhotonCommon::CharType* Message)
{
#if WITH_EDITOR
	if (GEngine)
	{
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Emerald, FString::Printf(TEXT("%s"), MessageString.Get()));
	}
#endif
}

void FusionOnScreenDebugMessageLogOutput::LogDebug(const PhotonCommon::CharType* Message)
{
#if WITH_EDITOR
	if (GEngine)
	{
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Emerald, FString::Printf(TEXT("%s"), MessageString.Get()));
	}
#endif
}

void FusionOnScreenDebugMessageLogOutput::LogInfo(const PhotonCommon::CharType* Message)
{
#if WITH_EDITOR
	if (GEngine)
	{
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Emerald, FString::Printf(TEXT("%s"), MessageString.Get()));
	}
#endif
}

void FusionOnScreenDebugMessageLogOutput::LogWarning(const PhotonCommon::CharType* Message)
{
#if WITH_EDITOR
	if (GEngine)
	{
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Yellow, FString::Printf(TEXT("%s"), MessageString.Get()));
	}
#endif
}

void FusionOnScreenDebugMessageLogOutput::LogError(const PhotonCommon::CharType* Message)
{
#if WITH_EDITOR
	if (GEngine)
	{
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, FString::Printf(TEXT("%s"), MessageString.Get()));
	}
#endif
}
