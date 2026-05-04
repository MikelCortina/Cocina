// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Logging/FusionStandardLogOutput.h"
#include "FusionShared.h"
#include "Logging/LogMacros.h"

void FusionStandardLogOutput::LogTrace(const PhotonCommon::CharType* Message)
{
	const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
	UE_LOG(LogFusion, Display, TEXT("%s"), MessageString.Get());
}

void FusionStandardLogOutput::LogDebug(const PhotonCommon::CharType* Message)
{
	const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
	UE_LOG(LogFusion, Display, TEXT("%s"), MessageString.Get());
}

void FusionStandardLogOutput::LogInfo(const PhotonCommon::CharType* Message)
{
	const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
	UE_LOG(LogFusion, Display, TEXT("%s"), MessageString.Get());
}

void FusionStandardLogOutput::LogWarning(const PhotonCommon::CharType* Message)
{
	const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
	UE_LOG(LogFusion, Warning, TEXT("%s"), MessageString.Get());
}

void FusionStandardLogOutput::LogError(const PhotonCommon::CharType* Message)
{
	const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> MessageString = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Message));
	UE_LOG(LogFusion, Error, TEXT("%s"), MessageString.Get());
}
