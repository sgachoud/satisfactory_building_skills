#include "BSStick.h"
#include "Configuration/ConfigManager.h"
#include "Engine/GameInstance.h"

bool FBSStickSettings::Valid() const
{
    return FMath::IsFinite(StickDistance) && StickDistance >= 0 && StickDistance <= 100000 &&
        FMath::IsFinite(StickStep) && StickStep > 0 && StickStep <= 1000 &&
        FMath::IsFinite(StickTiltStep) && StickTiltStep > 0 && StickTiltStep <= 90 &&
        FMath::IsFinite(StickSlope) && FMath::Abs(StickSlope) < 89.9 &&
        FMath::IsFinite(StickRadius) && (StickRadius == 0 || FMath::Abs(StickRadius) >= .01) && FMath::Abs(StickRadius) <= 100000;
}
FBSStickSettings FBSStickSettings::Get(UWorld* World)
{
    FBSStickSettings S;
    if (World && World->GetGameInstance())
        if (auto* C = World->GetGameInstance()->GetSubsystem<UConfigManager>())
            C->FillConfigurationStruct(FConfigId{TEXT("BuildingSkills"), TEXT("")}, FDynamicStructInfo{StaticStruct(), &S});
    return S.Valid() ? S : FBSStickSettings{};
}
void UBSStickSession::Initialize(FSubsystemCollectionBase& Collection)
{ Super::Initialize(Collection); Settings = FBSStickSettings::Get(GetWorld()); }

FVector FBSStickPath::EndpointDirection(const FVector& NativeTangent, bool OverridePitch, double Pitch, double YawOffset)
{
    FRotator Direction=NativeTangent.Rotation();
    if (OverridePitch) Direction.Pitch=Pitch;
    Direction.Yaw=FMath::UnwindDegrees(Direction.Yaw+YawOffset);
    return Direction.Vector();
}

double FBSStickPath::ResolveConstraints(const FBSStickSettings& Settings, const FVector& Target)
{
    Radius = Settings.HorizontalConstraint ? Settings.StickRadius * 100.0 : 0;
    Slope = Settings.VerticalConstraint ? FMath::DegreesToRadians(double(Settings.StickSlope)) : 0;
    if (!Settings.HorizontalConstraint)
    {
        // With free horizontal direction, the nearest fixed-grade ray points
        // toward the target in XY. The caller restores start-connector continuity.
        const FVector Delta = Target - Start;
        Forward = FVector(Delta.X, Delta.Y, 0).GetSafeNormal(UE_SMALL_NUMBER,
            FVector(Forward.X, Forward.Y, 0).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
    }
    const double Distance = Closest(Target);
    if (!Settings.VerticalConstraint && Distance > .01)
        Slope = FMath::Atan2(Target.Z - Start.Z, Distance);
    return Distance;
}

FVector FBSStickPath::Position(double S) const
{
    const FVector F = FVector(Forward.X, Forward.Y, 0).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, F);
    const double X = Radius == 0 ? S : Radius * FMath::Sin(S / Radius);
    const double Y = Radius == 0 ? 0 : Radius * (1 - FMath::Cos(S / Radius));
    return Start + F * X + Right * Y + FVector::UpVector * (S * FMath::Tan(Slope));
}
FVector FBSStickPath::Derivative(double S) const
{
    const FVector F = FVector(Forward.X, Forward.Y, 0).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, F);
    return F * (Radius == 0 ? 1 : FMath::Cos(S / Radius)) +
        Right * (Radius == 0 ? 0 : FMath::Sin(S / Radius)) + FVector::UpVector * FMath::Tan(Slope);
}
double FBSStickPath::Closest(const FVector& Target) const
{
    if (Radius == 0)
    {
        const FVector D = Derivative(0);
        return FMath::Max(0.0, FVector::DotProduct(Target - Start, D) / D.SizeSquared());
    }
    // For any phase, all turns have identical XY. Only the nearest nonnegative
    // turn in Z can win. Search phase, not thousands of windings. Add the turn
    // rounding discontinuity explicitly so minima at branch boundaries survive.
    const double Period = 2 * UE_DOUBLE_PI * FMath::Abs(Radius);
    const double Grade = FMath::Tan(Slope);
    auto Choose = [&](double Phase)
    {
        double Turn = 0;
        if (FMath::Abs(Grade) > 1.e-12)
            Turn = FMath::Max(0.0, FMath::RoundToDouble(((Target.Z - Start.Z) / Grade - Phase) / Period));
        return Phase + Turn * Period;
    };
    double Best = 0, Score = FVector::DistSquared(Start, Target);
    auto Consider = [&](double Phase)
    {
        const double S = Choose(Phase), D = FVector::DistSquared(Position(S), Target);
        if (D < Score - 1.e-8 || (FMath::IsNearlyEqual(D, Score, 1.e-8) && S < Best)) { Best = S; Score = D; }
    };
    TArray<double> Cuts;
    for (int32 I = 0; I <= 128; ++I) Cuts.Add(Period * I / 128);
    if (FMath::Abs(Grade) > 1.e-12)
    {
        double Boundary = FMath::Fmod((Target.Z - Start.Z) / Grade + .5 * Period, Period);
        if (Boundary < 0) Boundary += Period;
        Cuts.Add(Boundary);
    }
    Cuts.Sort();
    for (int32 I = 1; I < Cuts.Num(); ++I)
    {
        double L = Cuts[I-1], R = Cuts[I];
        Consider(L); Consider(R);
        // Each short smooth interval contains at most one minimum of the
        // circular XY distance plus the quadratic vertical error.
        for (int32 J = 0; J < 40; ++J)
        {
            const double A = L + (R-L) * .38196601125, B = L + (R-L) * .61803398875;
            if (FVector::DistSquared(Position(Choose(A)), Target) < FVector::DistSquared(Position(Choose(B)), Target)) R = B;
            else L = A;
        }
        Consider((L+R)*.5);
    }
    return Best;
}
bool FBSStickPath::Segments(double Distance, double MaxLength, TArray<TArray<FSplinePointData>>& Out) const
{
    Out.Reset();
    if (!FMath::IsFinite(Distance) || Distance <= .01 || MaxLength <= 1) return false;
    const double Length = Distance / FMath::Cos(Slope);
    if (Length > MaxLength * 256) return false;
    int32 Count = FMath::Max(1, FMath::CeilToInt(Length / (MaxLength - .1)));
    while (Radius != 0 && Distance / Count / FMath::Abs(Radius) > UE_DOUBLE_PI) Count *= 2;
    if (Count > 256) return false;
    // Cubic circular arcs use <= 15 degree subarcs, independent of buildable
    // segmentation. Analytic derivative keeps grade constant at every knot.
    for (int32 I = 0; I < Count; ++I)
    {
        auto& Part = Out.AddDefaulted_GetRef();
        const double A = Distance * I / Count, B = Distance * (I+1) / Count;
        const int32 Knots = Radius == 0 ? 1 : FMath::Max(1, FMath::CeilToInt((B-A) / FMath::Abs(Radius) / (UE_DOUBLE_PI/12)));
        const double Step = (B-A) / Knots;
        for (int32 K = 0; K <= Knots; ++K)
        {
            const double S = A + Step*K;
            Part.Emplace(Position(S), Derivative(S)*Step, Derivative(S)*Step);
        }
    }
    return true;
}
bool FBSStickPath::FitEndDirections(TArray<TArray<FSplinePointData>>& Parts, const FVector& StartDirection, const FVector& EndDirection, double MaxLength)
{
    if (Parts.IsEmpty()) return false;
    TArray<TArray<FSplinePointData>> Result;
    for (int32 I=0; I<Parts.Num(); ++I)
    {
        auto Part=Parts[I];
        if (Part.Num()<2) return false;
        const double N=Part.Num()-1;
        const FVector Start=I==0 ? StartDirection.GetSafeNormal() : FVector::ZeroVector;
        const FVector End=I==Parts.Num()-1 ? EndDirection.GetSafeNormal() : FVector::ZeroVector;
        const FVector A=Start.IsNearlyZero() ? FVector::ZeroVector : (Start*Part[0].LeaveTangent.Size()-Part[0].LeaveTangent)*N;
        const FVector B=End.IsNearlyZero() ? FVector::ZeroVector : (End*Part.Last().ArriveTangent.Size()-Part.Last().ArriveTangent)*N;
        if (A.IsNearlyZero(.001) && B.IsNearlyZero(.001)) { Result.Add(MoveTemp(Part)); continue; }
        // A cubic correction is zero at both ends. Its endpoint derivatives
        // supply the requested directions and fade smoothly across the whole
        // first/last piece, rather than bending only the last tiny arc knot.
        for (int32 K=0; K<Part.Num(); ++K)
        {
            const double U=K/N;
            Part[K].Location+=A*(U*U*U-2*U*U+U)+B*(U*U*U-U*U);
            const FVector D=(A*(3*U*U-4*U+1)+B*(3*U*U-2*U))/N;
            Part[K].ArriveTangent+=D; Part[K].LeaveTangent+=D;
        }
        if (!Start.IsNearlyZero()) Part[0].ArriveTangent=Part[0].LeaveTangent;
        if (!End.IsNearlyZero()) Part.Last().LeaveTangent=Part.Last().ArriveTangent;
        // Endpoint transitions can lengthen a curve. Re-split the actual cubic
        // before native validation and transport; no overlong piece is hidden.
        TArray<TArray<FSplinePointData>> SplitParts;
        if (!Split(Part,MaxLength,SplitParts)) return false;
        Result.Append(MoveTemp(SplitParts));
        if (Result.Num()>256) return false;
    }
    if (Result.Num()>256) return false;
    Parts=MoveTemp(Result); return true;
}
bool FBSStickPath::Split(const TArray<FSplinePointData>& Points, double MaxLength, TArray<TArray<FSplinePointData>>& Out)
{
    Out.Reset();
    if (Points.Num() < 2 || MaxLength <= 1) return false;
    auto* Curve = NewObject<USplineComponent>();
    Curve->ClearSplinePoints(false);
    for (int32 I=0; I<Points.Num(); ++I)
        Curve->AddPoint(FSplinePoint(I, Points[I].Location, Points[I].ArriveTangent, Points[I].LeaveTangent,
            FRotator::ZeroRotator, FVector::OneVector, ESplinePointType::CurveCustomTangent), false);
    Curve->UpdateSpline();
    const double Length = Curve->GetSplineLength();
    if (!FMath::IsFinite(Length) || Length > 256 * (MaxLength-.1)) return false;
    const int32 Count = FMath::Max(1, FMath::CeilToInt(Length/(MaxLength-.1)));
    for (int32 I=0; I<Count; ++I)
    {
        const double A = Curve->GetInputKeyValueAtDistanceAlongSpline(Length*I/Count);
        const double B = Curve->GetInputKeyValueAtDistanceAlongSpline(Length*(I+1)/Count);
        TArray<double> Keys{A};
        for (int32 K=FMath::FloorToInt(A)+1; K<B-1.e-7; ++K) Keys.Add(K);
        Keys.Add(B);
        auto& Part = Out.AddDefaulted_GetRef();
        for (int32 K=0; K<Keys.Num(); ++K)
        {
            const double Key=Keys[K];
            // Preserve each original cubic exactly, including separate arrive
            // and leave tangents at native routing corners.
            const int32 Index=FMath::Clamp(FMath::FloorToInt(Key),0,Points.Num()-2);
            const double T=Key-Index;
            FVector Arrive, Leave, P;
            const int32 Knot=FMath::RoundToInt(Key);
            if (FMath::IsNearlyEqual(Key,double(Knot),1.e-6))
            { P=Points[Knot].Location; Arrive=Points[Knot].ArriveTangent; Leave=Points[Knot].LeaveTangent; }
            else
            {
                const auto& X=Points[Index]; const auto& Y=Points[Index+1];
                P=FMath::CubicInterp(X.Location,X.LeaveTangent,Y.Location,Y.ArriveTangent,T);
                Arrive=Leave=(6*T*T-6*T)*X.Location+(3*T*T-4*T+1)*X.LeaveTangent+(-6*T*T+6*T)*Y.Location+(3*T*T-2*T)*Y.ArriveTangent;
            }
            Part.Emplace(P,Arrive*(K ? Key-Keys[K-1] : Keys[1]-Key),Leave*(K+1<Keys.Num() ? Keys[K+1]-Key : Key-Keys[K-1]));
        }
    }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSStickMathTest,"BuildingSkills.Stick.Path",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSStickMathTest::RunTest(const FString&)
{
    const FVector NativeEnd=FRotator(20,170,0).Vector();
    TestTrue(TEXT("Horizontal endpoint tilt preserves native pitch and wraps yaw"),FBSStickPath::EndpointDirection(NativeEnd,false,0,30).Equals(FRotator(20,-160,0).Vector(),.0001));
    TestTrue(TEXT("Combined endpoint tilt keeps each axis independent"),FBSStickPath::EndpointDirection(NativeEnd,true,-15,-45).Equals(FRotator(-15,125,0).Vector(),.0001));
    TestTrue(TEXT("Zero horizontal offset preserves the native tangent"),FBSStickPath::EndpointDirection(NativeEnd,false,0,0).Equals(NativeEnd,.0001));
    FBSStickSettings Settings;
    TestTrue(TEXT("Existing constraint behavior is enabled by default"),Settings.HorizontalConstraint && Settings.VerticalConstraint);
    Settings.StickRadius=10; Settings.StickSlope=30;
    FBSStickPath FreeHeight;
    Settings.VerticalConstraint=false;
    const FVector HighTarget(1000,1000,700);
    const double Arc=FreeHeight.ResolveConstraints(Settings,HighTarget);
    TestTrue(TEXT("Horizontal-only constraint keeps radius but releases height"),FreeHeight.Position(Arc).Equals(HighTarget,.01));
    TestEqual(TEXT("Disabling slope does not discard its setting"),Settings.StickSlope,30.f);
    FBSStickPath FreeHeading;
    Settings.HorizontalConstraint=false; Settings.VerticalConstraint=true; Settings.StickSlope=0;
    const FVector SideTarget(400,1200,500);
    const double Straight=FreeHeading.ResolveConstraints(Settings,SideTarget);
    TestTrue(TEXT("Vertical-only zero slope allows sideways movement while holding height"),FreeHeading.Position(Straight).Equals(FVector(400,1200,0),.01));
    Settings.StickSlope=FMath::RadiansToDegrees(FMath::Atan(.1));
    FBSStickPath GradePath;
    const double GradeDistance=GradePath.ResolveConstraints(Settings,FVector(300,400,50));
    TestTrue(TEXT("Vertical-only nonzero grade reaches an off-axis target on its slope"),GradePath.Position(GradeDistance).Equals(FVector(300,400,50),.01));
    TestEqual(TEXT("Disabling radius does not discard its setting"),Settings.StickRadius,10.f);
    Settings.HorizontalConstraint=true; Settings.VerticalConstraint=false;
    FBSStickPath AtStart;
    TestEqual(TEXT("Free height directly above starting XY has zero horizontal travel"),AtStart.ResolveConstraints(Settings,FVector(0,0,1000)),0.0);
    TestTrue(TEXT("Vertical-only target does not produce a nonfinite slope"),FMath::IsFinite(AtStart.Slope));
    Settings.VerticalConstraint=true;
    FBSStickPath Restored; const FVector HelixTarget(1000,1000,500);
    Restored.ResolveConstraints(Settings,HelixTarget);
    TestTrue(TEXT("Re-enabling restores stored radius and slope"),Restored.Radius==1000 && FMath::IsNearlyEqual(Restored.Slope,FMath::DegreesToRadians(double(Settings.StickSlope)),1.e-9));
    FBSStickPath P;
    TestTrue(TEXT("Straight nearest point projects forward"),FMath::IsNearlyEqual(P.Closest(FVector(500,400,300)),500.0,.001));
    TestEqual(TEXT("Targets behind the start clamp to start"),P.Closest(FVector(-500,200,100)),0.0);
    P.Slope=FMath::Atan(.1);
    TestTrue(TEXT("Ten percent slope rises one meter per ten horizontal meters"),P.Position(1000).Equals(FVector(1000,0,100),.001));
    for (double Radius : {5000.0,-5000.0}) for (double Grade : {.1,-.1})
    {
        P.Radius=Radius; P.Slope=FMath::Atan(Grade);
        const double D=FMath::Abs(Radius)*(8*UE_DOUBLE_PI+.7);
        const FVector Target=P.Position(D);
        TestTrue(TEXT("Nearest point includes later turns in either direction and slope"),P.Position(P.Closest(Target)).Equals(Target,.01));
        TArray<TArray<FSplinePointData>> Segments;
        TestTrue(TEXT("Helix divides into constructible length pieces"),P.Segments(D,5600,Segments));
        for (int32 I=1; I<Segments.Num(); ++I)
        {
            TestTrue(TEXT("Adjacent segments meet exactly"),Segments[I-1].Last().Location.Equals(Segments[I][0].Location,.00001));
            TestTrue(TEXT("Adjacent segments have aligned tangents"),Segments[I-1].Last().ArriveTangent.GetSafeNormal().Equals(Segments[I][0].LeaveTangent.GetSafeNormal(),.00001));
        }
    }
    P.Slope=0; P.Radius=1000;
    TArray<TArray<FSplinePointData>> Parts;
    TestTrue(TEXT("Long turn splits even without length restriction"),P.Segments(3.5*UE_DOUBLE_PI*1000,100000,Parts));
    TestEqual(TEXT("Over-half-circle pieces are halved"),Parts.Num(),4);
    TArray<FSplinePointData> Native{FSplinePointData(FVector::ZeroVector,FVector(6000,0,0)),FSplinePointData(FVector(6000,6000,0),FVector(0,6000,0))};
    TestTrue(TEXT("Native routed cubic splits"),FBSStickPath::Split(Native,3000,Parts));
    for (const auto& Part:Parts) for (const auto& Point:Part)
    {
        double Closest=MAX_dbl;
        for (int32 I=0; I<=10000; ++I)
            Closest=FMath::Min(Closest,FVector::Dist(Point.Location,FMath::CubicInterp(Native[0].Location,Native[0].LeaveTangent,Native[1].Location,Native[1].ArriveTangent,I/10000.0)));
        TestTrue(TEXT("Split stays on original native curve"),Closest<1);
    }
    return true;
}
#endif
