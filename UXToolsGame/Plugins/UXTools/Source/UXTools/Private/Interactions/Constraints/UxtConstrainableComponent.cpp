// Copyright (c) 2020 Microsoft Corporation.
// Licensed under the MIT License.

#include "Interactions/Constraints/UxtConstrainableComponent.h"

#include "UXTools.h"

namespace
{
	const float kRelativeScaleFloor = 0.01f;
	const float kRelativeScaleCeiling = 1.0f;

	void ApplyImplicitScalingConstraint(FTransform& Transform, const FVector& MinScale, const FVector& MaxScale)
	{
		FVector ConstrainedScale = Transform.GetScale3D();
		ConstrainedScale.X = FMath::Clamp(ConstrainedScale.X, MinScale.X, MaxScale.X);
		ConstrainedScale.Y = FMath::Clamp(ConstrainedScale.Y, MinScale.Y, MaxScale.Y);
		ConstrainedScale.Z = FMath::Clamp(ConstrainedScale.Z, MinScale.Z, MaxScale.Z);
		Transform.SetScale3D(ConstrainedScale);
	}
} // namespace

bool UUxtConstrainableComponent::GetAutoDetectConstraints() const
{
	return bAutoDetectConstraints;
}

void UUxtConstrainableComponent::SetAutoDetectConstraints(bool bNewAutoDetectConstraints)
{
	bAutoDetectConstraints = bNewAutoDetectConstraints;
	UpdateActiveConstraints();
}

const TArray<FComponentReference>& UUxtConstrainableComponent::GetSelectedConstraints() const
{
	return SelectedConstraints;
}

void UUxtConstrainableComponent::AddConstraint(const FComponentReference& NewConstraint)
{
	if (bAutoDetectConstraints)
	{
		UE_LOG(UXTools, Warning, TEXT("Manually adding a constraint to a UxtConstrainableComponent using automatic constraint detection."));
	}

	SelectedConstraints.Add(NewConstraint);
	UpdateActiveConstraints();
}

void UUxtConstrainableComponent::RemoveConstraint(const FComponentReference& NewConstraint)
{
	if (bAutoDetectConstraints)
	{
		UE_LOG(
			UXTools, Warning,
			TEXT("Manually removing a constraint from a UxtConstrainableComponent using automatic constraint detection."));
	}

	SelectedConstraints.Remove(NewConstraint);
	UpdateActiveConstraints();
}

void UUxtConstrainableComponent::SetRelativeToInitialScale(const bool Value)
{
	if (bRelativeToInitialScale != Value)
	{
		ConvertMinMaxScaleValues();
	}
}

void UUxtConstrainableComponent::SetMinScale(const float Value)
{
	const float CeilingValue = bRelativeToInitialScale ? kRelativeScaleCeiling : MaxScale;
	MinScale = FMath::Clamp(Value, kRelativeScaleFloor, CeilingValue);
}

void UUxtConstrainableComponent::SetMaxScale(const float Value)
{
	const float FloorValue = bRelativeToInitialScale ? kRelativeScaleCeiling : MinScale;
	MaxScale = FMath::Max(Value, FloorValue);
}

void UUxtConstrainableComponent::BeginPlay()
{
	Super::BeginPlay();

	FTransform ReferenceTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;

	InitialScale = ReferenceTransform.GetScale3D();

	if (GetOwner())
	{
		// Constrain initial transform, in order to prevent scale from jumping on the first interaction
		FTransform ConstrainedTransform = ReferenceTransform;
		ApplyImplicitScalingConstraint(ConstrainedTransform, GetMinScaleVec(), GetMaxScaleVec());
		GetOwner()->SetActorTransform(ConstrainedTransform);
	}
}

void UUxtConstrainableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAutoDetectConstraints)
	{
		UpdateActiveConstraints();
	}
}

#if WITH_EDITOR
void UUxtConstrainableComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FProperty* Property = PropertyChangedEvent.Property)
	{
		const FName PropertyName = Property->GetFName();
		if (PropertyName.IsEqual(GET_MEMBER_NAME_CHECKED(UUxtConstrainableComponent, bRelativeToInitialScale)))
		{
			InitialScale = GetOwner() ? GetOwner()->GetActorScale3D() : FVector::OneVector;
			ConvertMinMaxScaleValues();
		}
		else if (PropertyName.IsEqual(GET_MEMBER_NAME_CHECKED(UUxtConstrainableComponent, MinScale)))
		{
			SetMinScale(MinScale);
		}
		else if (PropertyName.IsEqual(GET_MEMBER_NAME_CHECKED(UUxtConstrainableComponent, MaxScale)))
		{
			SetMaxScale(MaxScale);
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

void UUxtConstrainableComponent::InitializeConstraints(USceneComponent* NewTargetComponent)
{
	check(NewTargetComponent);
	TargetComponent = NewTargetComponent;
	ActiveConstraints = GetConstraints();

	for (UUxtTransformConstraint* Constraint : ActiveConstraints)
	{
		Constraint->Initialize(TargetComponent->GetComponentTransform());
	}
}

void UUxtConstrainableComponent::ApplyConstraints(
	FTransform& Transform, EUxtTransformMode TransformMode, bool bIsOneHanded, bool bIsNear) const
{
	const int32 GrabMode = static_cast<int32>(bIsOneHanded ? EUxtGrabMode::OneHanded : EUxtGrabMode::TwoHanded);
	const int32 InteractionMode = static_cast<int32>(bIsNear ? EUxtInteractionMode::Near : EUxtInteractionMode::Far);

	if (TransformMode == EUxtTransformMode::Scaling)
	{
		ApplyImplicitScalingConstraint(Transform, GetMinScaleVec(), GetMaxScaleVec());
	}

	for (const UUxtTransformConstraint* Constraint : ActiveConstraints)
	{
		if (Constraint->GetConstraintType() == TransformMode && Constraint->HandType & GrabMode &&
			Constraint->InteractionMode & InteractionMode)
		{
			Constraint->ApplyConstraint(Transform);
		}
	}
}

TArray<UUxtTransformConstraint*> UUxtConstrainableComponent::GetConstraints() const
{
	TArray<UUxtTransformConstraint*> Constraints;

	if (bAutoDetectConstraints)
	{
		if (GetOwner())
		{
			GetOwner()->GetComponents<UUxtTransformConstraint>(Constraints);
		}
	}
	else
	{
		Constraints.Reserve(SelectedConstraints.Num());

		for (const FComponentReference& ConstraintReference : SelectedConstraints)
		{
			if (UUxtTransformConstraint* Constraint = Cast<UUxtTransformConstraint>(ConstraintReference.GetComponent(GetOwner())))
			{
				Constraints.Add(Constraint);
			}
		}
	}

	return Constraints;
}

void UUxtConstrainableComponent::UpdateActiveConstraints()
{
	if (!TargetComponent)
	{
		// Don't update constraints if we don't have a target component.
		return;
	}

	int NewConstraints = 0;
	int ExistingConstraints = 0;
	TArray<UUxtTransformConstraint*> Constraints = GetConstraints();

	for (UUxtTransformConstraint* Constraint : Constraints)
	{
		if (ActiveConstraints.Contains(Constraint))
		{
			ExistingConstraints++;
		}
		else
		{
			Constraint->Initialize(TargetComponent->GetComponentTransform());
			NewConstraints++;
		}
	}

	if (NewConstraints > 0 || ExistingConstraints < ActiveConstraints.Num())
	{
		ActiveConstraints = Constraints;
	}
}

void UUxtConstrainableComponent::ConvertMinMaxScaleValues()
{
	const float Min = InitialScale.GetMin();
	const float Max = InitialScale.GetMax();
	if (bRelativeToInitialScale) // Absolute -> relative
	{
		SetMinScale(MinScale / Min);
		SetMaxScale(MaxScale / Max);
	}
	else // Relative -> absolute
	{
		SetMinScale(MinScale * Min);
		SetMaxScale(MaxScale * Max);
	}
}

FVector UUxtConstrainableComponent::GetMinScaleVec() const
{
	FVector MinScaleVec(MinScale);
	if (bRelativeToInitialScale)
	{
		MinScaleVec *= InitialScale;
	}
	return MinScaleVec;
}

FVector UUxtConstrainableComponent::GetMaxScaleVec() const
{
	FVector MaxScaleVec(MaxScale);
	if (bRelativeToInitialScale)
	{
		MaxScaleVec *= InitialScale;
	}
	return MaxScaleVec;
}
