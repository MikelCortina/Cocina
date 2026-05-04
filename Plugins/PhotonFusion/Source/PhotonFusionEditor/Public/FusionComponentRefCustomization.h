// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IPropertyTypeCustomization.h"
#include "Widgets/SWidget.h"

class FFusionComponentRefCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren( TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils ) override;
	bool ShouldFilterComponent(const FAssetData& AssetData) const;
	void OnSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> GenerateComboItem(TSharedPtr<FString> InItem);

private:
	TSharedPtr<IPropertyHandle> ComponentProperty;

	TArray<TSharedPtr<FString>> ComponentNameOptions;
	TSharedPtr<IPropertyHandle> ComponentNameProperty;
	TSharedPtr<FString> CurrentSelection;
};
