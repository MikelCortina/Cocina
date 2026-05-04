// Copyright 2026 Exit Games GmbH. All Rights Reserved.


#include "FusionComponentRefCustomization.h"

#include "GameFramework/Actor.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SComboBox.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "FusionActorComponent.h"

AActor* GetOwningActor(TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	TArray<UObject*> OuterObjects;
	StructPropertyHandle->GetOuterObjects(OuterObjects);

	for (UObject* Outer : OuterObjects)
	{
		if (UFusionActorComponent* AsComp = Cast<UFusionActorComponent>(Outer))
		{
			UBlueprintGeneratedClass* OwnerClass = Cast<UBlueprintGeneratedClass>(AsComp->GetOuter());
			if (OwnerClass)
			{
				if (AActor* ActorCDO = Cast<AActor>(OwnerClass->GetDefaultObject()))
				{
					return ActorCDO;
				}
			}
		}
	}
	return nullptr;
}

TSharedRef<IPropertyTypeCustomization> FFusionComponentRefCustomization::MakeInstance()
{
	return MakeShareable(new FFusionComponentRefCustomization());
}

void FFusionComponentRefCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
                                                       FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ComponentProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFusionComponentRef, ComponentName));

	ComponentNameOptions.Empty();
	
	AActor* OwningActor = GetOwningActor(StructPropertyHandle);
	if (OwningActor)
	{
		auto allComponents = OwningActor->GetComponents();

		for (auto component : allComponents)
		{
			ComponentNameOptions.Add(MakeShared<FString>(component->GetName()));
		}
	}

	ComponentNameOptions.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B) {
	return *A < *B;
	});

	ComponentNameProperty = StructPropertyHandle->GetChildHandle(TEXT("ComponentName"));

	FString CurrentValue;
	ComponentNameProperty->GetValue(CurrentValue);
	CurrentSelection = MakeShared<FString>(CurrentValue);
	
	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(200.f)
	[
		SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&ComponentNameOptions)
		.OnSelectionChanged(this, &FFusionComponentRefCustomization::OnSelectionChanged)
		.OnGenerateWidget(this, &FFusionComponentRefCustomization::GenerateComboItem)
		.InitiallySelectedItem(CurrentSelection)
		[
			SNew(STextBlock).Text_Lambda([this]() {
				return CurrentSelection.IsValid() ? FText::FromString(*CurrentSelection) : FText::FromString(TEXT("Select Component"));
			})
		]
	];
}

void FFusionComponentRefCustomization::OnSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if (NewValue.IsValid())
	{
		CurrentSelection = NewValue;
		ComponentNameProperty->SetValue(*NewValue);
	}
}

TSharedRef<SWidget> FFusionComponentRefCustomization::GenerateComboItem(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}


void FFusionComponentRefCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

bool FFusionComponentRefCustomization::ShouldFilterComponent(const FAssetData& AssetData) const
{
	return false;
}
