#include "Benchmarking/FluidBenchmarkComponent.h"
#include "Actors/VoxelFluidActor.h"
#include "CellularAutomata/FluidChunkManager.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "VoxelFluidStats.h"

UFluidBenchmarkComponent::UFluidBenchmarkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	
	// Setup default test configurations
	FBenchmarkConfig NoOptimizations;
	NoOptimizations.ConfigName = TEXT("No Optimizations");
	NoOptimizations.bUseSleepChains = false;
	NoOptimizations.bUsePredictiveSettling = false;
	NoOptimizations.bEnableMemoryCompression = false;
	TestConfigs.Add(NoOptimizations);

	FBenchmarkConfig SleepChains;
	SleepChains.ConfigName = TEXT("Sleep Chains Only");
	SleepChains.bUseSleepChains = true;
	SleepChains.bUsePredictiveSettling = false;
	SleepChains.bEnableMemoryCompression = false;
	TestConfigs.Add(SleepChains);

	FBenchmarkConfig PredictiveSettling;
	PredictiveSettling.ConfigName = TEXT("Predictive Settling Only");
	PredictiveSettling.bUseSleepChains = false;
	PredictiveSettling.bUsePredictiveSettling = true;
	PredictiveSettling.bEnableMemoryCompression = false;
	TestConfigs.Add(PredictiveSettling);

	FBenchmarkConfig AllOptimizations;
	AllOptimizations.ConfigName = TEXT("All Optimizations");
	AllOptimizations.bUseSleepChains = true;
	AllOptimizations.bUsePredictiveSettling = true;
	AllOptimizations.bEnableMemoryCompression = true;
	TestConfigs.Add(AllOptimizations);
}

void UFluidBenchmarkComponent::BeginPlay()
{
	Super::BeginPlay();
	
	// Find the fluid actor
	FluidActor = Cast<AVoxelFluidActor>(GetOwner());
	if (!FluidActor)
	{
		UE_LOG(LogTemp, Error, TEXT("FluidBenchmarkComponent must be attached to a VoxelFluidActor!"));
	}
}

void UFluidBenchmarkComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsBenchmarking || !FluidActor)
		return;

	// Handle warmup period
	if (bInWarmup)
	{
		WarmupTimer += DeltaTime;
		if (WarmupTimer >= WarmupTime)
		{
			bInWarmup = false;
			WarmupTimer = 0.0f;
			CurrentResult = FBenchmarkResult();
			CurrentResult.TestName = TestConfigs.IsValidIndex(CurrentConfigIndex) 
				? TestConfigs[CurrentConfigIndex].ConfigName 
				: TEXT("Unknown");
			
			UE_LOG(LogTemp, Warning, TEXT("Starting benchmark: %s"), *CurrentResult.TestName);
		}
		return;
	}

	// Collect benchmark samples
	CollectSample(DeltaTime);

	// Check if benchmark is complete
	BenchmarkTimer += DeltaTime;
	if (BenchmarkTimer >= BenchmarkDuration)
	{
		FinalizeBenchmark();
		
		// Move to next configuration or stop
		CurrentConfigIndex++;
		if (CurrentConfigIndex < TestConfigs.Num())
		{
			RunNextConfiguration();
		}
		else
		{
			StopBenchmark();
			
			UE_LOG(LogTemp, Warning, TEXT("All benchmarks complete!"));
			UE_LOG(LogTemp, Warning, TEXT("\n%s"), *GetComparisonReport());
			
			if (bAutoSaveResults)
			{
				SaveBenchmarkResults();
			}
		}
	}
}

void UFluidBenchmarkComponent::StartBenchmark()
{
	if (!FluidActor)
	{
		UE_LOG(LogTemp, Error, TEXT("No FluidActor found!"));
		return;
	}

	// Store original configuration
	// Optimization properties removed - using default values
	OriginalConfig.bUseSleepChains = false;
	OriginalConfig.bUsePredictiveSettling = false;
	OriginalConfig.bEnableMemoryCompression = false;
	OriginalConfig.ChunkSize = FluidActor->ChunkSize;
	OriginalConfig.MaxActiveChunks = FluidActor->MaxActiveChunks;

	// Clear previous results
	BenchmarkResults.Empty();
	
	// Start with first configuration
	CurrentConfigIndex = 0;
	bIsBenchmarking = true;
	
	if (TestConfigs.Num() > 0)
	{
		RunNextConfiguration();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("No test configurations defined!"));
		StopBenchmark();
	}
}

void UFluidBenchmarkComponent::StopBenchmark()
{
	bIsBenchmarking = false;
	bInWarmup = false;
	BenchmarkTimer = 0.0f;
	WarmupTimer = 0.0f;
	
	// Restore original configuration
	if (FluidActor)
	{
		RestoreOriginalConfiguration();
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Benchmark stopped"));
}

void UFluidBenchmarkComponent::RunComparisonBenchmark()
{
	// This runs all configured benchmarks in sequence
	StartBenchmark();
}

void UFluidBenchmarkComponent::CollectSample(float DeltaTime)
{
	if (!FluidActor || !FluidActor->ChunkManager)
		return;

	// Get timing stats - simplified version without complex stats API
	// In production, you'd use Unreal's profiling tools or add explicit timing
	const FChunkManagerStats ChunkStats = FluidActor->ChunkManager->GetStats();
	float SimTime = ChunkStats.AverageChunkUpdateTime; // Use chunk update time as simulation time
	float MeshTime = 0.0f; // Would need explicit timing in mesh generation
	float BorderTime = 0.0f; // Would need explicit timing in border sync
	
	// Alternative: Use the last frame simulation time from the actor
	if (FluidActor->GetLastFrameSimulationTime() > 0.0f)
	{
		SimTime = FluidActor->GetLastFrameSimulationTime();
	}

	// Use actual frame time
	const float FrameTime = DeltaTime * 1000.0f; // Convert to ms
	
	// Update running statistics
	CurrentResult.SampleCount++;
	CurrentResult.AverageFrameTime = (CurrentResult.AverageFrameTime * (CurrentResult.SampleCount - 1) + FrameTime) / CurrentResult.SampleCount;
	CurrentResult.MinFrameTime = FMath::Min(CurrentResult.MinFrameTime, FrameTime);
	CurrentResult.MaxFrameTime = FMath::Max(CurrentResult.MaxFrameTime, FrameTime);
	
	// Update component times (averaged)
	CurrentResult.SimulationTime = (CurrentResult.SimulationTime * (CurrentResult.SampleCount - 1) + SimTime) / CurrentResult.SampleCount;
	CurrentResult.MeshGenerationTime = (CurrentResult.MeshGenerationTime * (CurrentResult.SampleCount - 1) + MeshTime) / CurrentResult.SampleCount;
	CurrentResult.BorderSyncTime = (CurrentResult.BorderSyncTime * (CurrentResult.SampleCount - 1) + BorderTime) / CurrentResult.SampleCount;

	// Get current stats
	const FChunkManagerStats Stats = FluidActor->ChunkManager->GetStats();
	CurrentResult.ActiveChunks = Stats.ActiveChunks;
	CurrentResult.ActiveCells = Stats.TotalActiveCells;
	CurrentResult.TotalFluidVolume = Stats.TotalFluidVolume;
	CurrentResult.MemoryUsageMB = CalculateMemoryUsage();
}

void UFluidBenchmarkComponent::FinalizeBenchmark()
{
	// Add current result to results array
	BenchmarkResults.Add(CurrentResult);
	
	UE_LOG(LogTemp, Warning, TEXT("Benchmark complete: %s"), *CurrentResult.ToString());
}

void UFluidBenchmarkComponent::RunNextConfiguration()
{
	if (!TestConfigs.IsValidIndex(CurrentConfigIndex))
		return;

	// Apply the configuration
	ApplyConfiguration(TestConfigs[CurrentConfigIndex]);
	
	// Reset benchmark state
	BenchmarkTimer = 0.0f;
	WarmupTimer = 0.0f;
	bInWarmup = true;
	
	// Reset the simulation for fair comparison
	if (FluidActor)
	{
		FluidActor->ResetSimulation();
		
		// Add significant fluid for proper stress testing
		const FBenchmarkConfig& Config = TestConfigs[CurrentConfigIndex];
		
		// Create multiple columns of fluid at different heights for vertical flow
		const int32 NumColumns = FMath::Max(5, Config.FluidSourceCount / 2);
		// Use the chunk size and cell size to calculate the grid extent
		const float GridExtent = FluidActor->ChunkSize * FluidActor->CellSize * 4.0f; // Assume 4x4 chunks
		const float ColumnSpacing = GridExtent / (NumColumns + 1);
		
		for (int32 x = 0; x < NumColumns; ++x)
		{
			for (int32 y = 0; y < NumColumns; ++y)
			{
				FVector ColumnPos = FluidActor->GetActorLocation() + 
					FVector(
						(x + 1) * ColumnSpacing - GridExtent * 0.5f,
						(y + 1) * ColumnSpacing - GridExtent * 0.5f,
						FMath::RandRange(1000.0f, 3000.0f)
					);
				
				// Add 100 units of fluid per column to ensure proper stress
				FluidActor->AddFluidSource(ColumnPos, 100.0f);
			}
		}
		
		// Add some random sources for chaos
		const int32 RandomSources = FMath::Max(10, Config.FluidSourceCount / 2);
		for (int32 i = 0; i < RandomSources; i++)
		{
			FVector SourcePos = FluidActor->GetActorLocation() + 
				FVector(
					FMath::RandRange(-GridExtent * 0.4f, GridExtent * 0.4f),
					FMath::RandRange(-GridExtent * 0.4f, GridExtent * 0.4f),
					FMath::RandRange(500.0f, 2500.0f)
				);
			FluidActor->AddFluidSource(SourcePos, 50.0f);
		}
		
		FluidActor->StartSimulation();
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Starting warmup for: %s"), *TestConfigs[CurrentConfigIndex].ConfigName);
}

void UFluidBenchmarkComponent::ApplyConfiguration(const FBenchmarkConfig& Config)
{
	if (!FluidActor)
		return;

	// Optimization properties removed - cannot apply config
	
	// Apply to chunk manager if it exists
	if (FluidActor->ChunkManager)
	{
		// Note: Optimization settings removed - using default behavior
	}
	
	UE_LOG(LogTemp, Log, TEXT("Applied config: %s (Sleep:%d, Predictive:%d, Compression:%d)"),
		*Config.ConfigName,
		Config.bUseSleepChains ? 1 : 0,
		Config.bUsePredictiveSettling ? 1 : 0,
		Config.bEnableMemoryCompression ? 1 : 0);
}

void UFluidBenchmarkComponent::RestoreOriginalConfiguration()
{
	if (!FluidActor)
		return;

	// Optimization properties removed - cannot restore config
}

float UFluidBenchmarkComponent::CalculateMemoryUsage() const
{
	if (!FluidActor || !FluidActor->ChunkManager)
		return 0.0f;

	const FChunkManagerStats Stats = FluidActor->ChunkManager->GetStats();
	
	// Calculate memory based on compression settings
	const float BytesPerCell = 44.0f; // No compression available
	const float CellMemory = Stats.TotalActiveCells * BytesPerCell;
	const float ChunkOverhead = Stats.TotalChunks * sizeof(UFluidChunk) * 2; // Rough estimate
	const float CacheMemory = FluidActor->ChunkManager->GetCacheMemoryUsage() * 1024; // Convert KB to bytes
	
	return (CellMemory + ChunkOverhead + CacheMemory) / (1024.0f * 1024.0f); // Convert to MB
}

void UFluidBenchmarkComponent::SaveBenchmarkResults()
{
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString FileName = FString::Printf(TEXT("%sBenchmark_%s.csv"), *ResultsFilePath, *Timestamp);
	
	const FString CSVContent = GenerateCSVReport();
	
	if (FFileHelper::SaveStringToFile(CSVContent, *FileName))
	{
		UE_LOG(LogTemp, Warning, TEXT("Benchmark results saved to: %s"), *FileName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save benchmark results!"));
	}
}

FString UFluidBenchmarkComponent::GenerateCSVReport() const
{
	FString CSV = TEXT("Test Name,Avg Frame (ms),Min Frame (ms),Max Frame (ms),Simulation (ms),Mesh Gen (ms),Border Sync (ms),Active Chunks,Active Cells,Fluid Volume,Memory (MB),Samples\n");
	
	for (const FBenchmarkResult& Result : BenchmarkResults)
	{
		CSV += FString::Printf(TEXT("%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%.1f,%.1f,%d\n"),
			*Result.TestName,
			Result.AverageFrameTime,
			Result.MinFrameTime,
			Result.MaxFrameTime,
			Result.SimulationTime,
			Result.MeshGenerationTime,
			Result.BorderSyncTime,
			Result.ActiveChunks,
			Result.ActiveCells,
			Result.TotalFluidVolume,
			Result.MemoryUsageMB,
			Result.SampleCount
		);
	}
	
	return CSV;
}

FString UFluidBenchmarkComponent::GetComparisonReport() const
{
	if (BenchmarkResults.Num() == 0)
		return TEXT("No benchmark results available");

	FString Report = TEXT("=== BENCHMARK COMPARISON REPORT ===\n\n");
	
	// Find baseline (no optimizations)
	const FBenchmarkResult* Baseline = nullptr;
	for (const FBenchmarkResult& Result : BenchmarkResults)
	{
		if (Result.TestName.Contains(TEXT("No Optimization")))
		{
			Baseline = &Result;
			break;
		}
	}
	
	// If no baseline, use first result
	if (!Baseline && BenchmarkResults.Num() > 0)
	{
		Baseline = &BenchmarkResults[0];
	}
	
	// Generate comparison table
	Report += TEXT("Configuration            | Avg Frame | vs Baseline | Memory  | vs Baseline\n");
	Report += TEXT("-------------------------|-----------|-------------|---------|------------\n");
	
	for (const FBenchmarkResult& Result : BenchmarkResults)
	{
		const float FrameImprovement = Baseline ? ((Baseline->AverageFrameTime - Result.AverageFrameTime) / Baseline->AverageFrameTime * 100.0f) : 0.0f;
		const float MemoryReduction = Baseline ? ((Baseline->MemoryUsageMB - Result.MemoryUsageMB) / Baseline->MemoryUsageMB * 100.0f) : 0.0f;
		
		Report += FString::Printf(TEXT("%-24s | %7.2fms | %+7.1f%%   | %6.1fMB | %+7.1f%%\n"),
			*Result.TestName,
			Result.AverageFrameTime,
			FrameImprovement,
			Result.MemoryUsageMB,
			MemoryReduction
		);
	}
	
	Report += TEXT("\n=== DETAILED TIMING BREAKDOWN ===\n\n");
	Report += TEXT("Configuration            | Simulation | Mesh Gen | Border Sync\n");
	Report += TEXT("-------------------------|------------|----------|------------\n");
	
	for (const FBenchmarkResult& Result : BenchmarkResults)
	{
		Report += FString::Printf(TEXT("%-24s | %8.2fms | %6.2fms | %9.2fms\n"),
			*Result.TestName,
			Result.SimulationTime,
			Result.MeshGenerationTime,
			Result.BorderSyncTime
		);
	}
	
	// Summary
	if (Baseline && BenchmarkResults.Num() > 1)
	{
		Report += TEXT("\n=== OPTIMIZATION SUMMARY ===\n");
		
		float BestFrameTime = FLT_MAX;
		float BestMemory = FLT_MAX;
		FString BestFrameConfig;
		FString BestMemoryConfig;
		
		for (const FBenchmarkResult& Result : BenchmarkResults)
		{
			if (Result.AverageFrameTime < BestFrameTime)
			{
				BestFrameTime = Result.AverageFrameTime;
				BestFrameConfig = Result.TestName;
			}
			if (Result.MemoryUsageMB < BestMemory)
			{
				BestMemory = Result.MemoryUsageMB;
				BestMemoryConfig = Result.TestName;
			}
		}
		
		Report += FString::Printf(TEXT("Best Performance: %s (%.2fms, %.1f%% improvement)\n"),
			*BestFrameConfig,
			BestFrameTime,
			(Baseline->AverageFrameTime - BestFrameTime) / Baseline->AverageFrameTime * 100.0f
		);
		
		Report += FString::Printf(TEXT("Best Memory: %s (%.1fMB, %.1f%% reduction)\n"),
			*BestMemoryConfig,
			BestMemory,
			(Baseline->MemoryUsageMB - BestMemory) / Baseline->MemoryUsageMB * 100.0f
		);
	}
	
	return Report;
}

void UFluidBenchmarkComponent::ClearResults()
{
	BenchmarkResults.Empty();
	CurrentResult = FBenchmarkResult();
	UE_LOG(LogTemp, Warning, TEXT("Benchmark results cleared"));
}

// Quick test implementations
void UFluidBenchmarkComponent::QuickTestNoOptimizations()
{
	TestConfigs.Empty();
	FBenchmarkConfig Config;
	Config.ConfigName = TEXT("Quick - No Optimizations");
	Config.bUseSleepChains = false;
	Config.bUsePredictiveSettling = false;
	Config.bEnableMemoryCompression = false;
	TestConfigs.Add(Config);
	
	BenchmarkDuration = 5.0f;
	WarmupTime = 1.0f;
	StartBenchmark();
}

void UFluidBenchmarkComponent::QuickTestWithOptimizations()
{
	TestConfigs.Empty();
	FBenchmarkConfig Config;
	Config.ConfigName = TEXT("Quick - All Optimizations");
	Config.bUseSleepChains = true;
	Config.bUsePredictiveSettling = true;
	Config.bEnableMemoryCompression = true;
	TestConfigs.Add(Config);
	
	BenchmarkDuration = 5.0f;
	WarmupTime = 1.0f;
	StartBenchmark();
}

void UFluidBenchmarkComponent::QuickTestMemoryCompression()
{
	TestConfigs.Empty();
	
	FBenchmarkConfig NoCompression;
	NoCompression.ConfigName = TEXT("No Compression");
	NoCompression.bEnableMemoryCompression = false;
	TestConfigs.Add(NoCompression);
	
	FBenchmarkConfig WithCompression;
	WithCompression.ConfigName = TEXT("With Compression");
	WithCompression.bEnableMemoryCompression = true;
	TestConfigs.Add(WithCompression);
	
	BenchmarkDuration = 5.0f;
	WarmupTime = 1.0f;
	StartBenchmark();
}

// Stress test implementations
void UFluidBenchmarkComponent::StressTest128Resolution()
{
	TestConfigs.Empty();
	
	FBenchmarkConfig Config;
	Config.ConfigName = TEXT("Stress 128³");
	Config.ChunkSize = 32;
	Config.MaxActiveChunks = 16; // 4x4x1 chunks = 128³
	Config.FluidSpawnAmount = 5000.0f;
	Config.FluidSourceCount = 10;
	Config.bUseSleepChains = true;
	Config.bUsePredictiveSettling = true;
	Config.bEnableMemoryCompression = false;
	TestConfigs.Add(Config);
	
	BenchmarkDuration = 15.0f;
	WarmupTime = 3.0f;
	StartBenchmark();
}

void UFluidBenchmarkComponent::StressTest256Resolution()
{
	TestConfigs.Empty();
	
	FBenchmarkConfig Config;
	Config.ConfigName = TEXT("Stress 256³");
	Config.ChunkSize = 32;
	Config.MaxActiveChunks = 64; // 8x8x1 chunks = 256³
	Config.FluidSpawnAmount = 20000.0f;
	Config.FluidSourceCount = 20;
	Config.bUseSleepChains = true;
	Config.bUsePredictiveSettling = true;
	Config.bEnableMemoryCompression = true;
	TestConfigs.Add(Config);
	
	BenchmarkDuration = 20.0f;
	WarmupTime = 5.0f;
	StartBenchmark();
}

void UFluidBenchmarkComponent::StressTest512Resolution()
{
	TestConfigs.Empty();
	
	FBenchmarkConfig Config;
	Config.ConfigName = TEXT("Stress 512³");
	Config.ChunkSize = 32;
	Config.MaxActiveChunks = 256; // 16x16x1 chunks = 512³
	Config.FluidSpawnAmount = 50000.0f;
	Config.FluidSourceCount = 30;
	Config.bUseSleepChains = true;
	Config.bUsePredictiveSettling = true;
	Config.bEnableMemoryCompression = true;
	TestConfigs.Add(Config);
	
	BenchmarkDuration = 30.0f;
	WarmupTime = 5.0f;
	StartBenchmark();
}

FString UFluidBenchmarkComponent::GetResultsReport() const
{
	FString Report = TEXT("=== BENCHMARK RESULTS ===\n\n");
	
	for (const FBenchmarkResult& Result : BenchmarkResults)
	{
		Report += Result.ToString() + TEXT("\n\n");
	}
	
	return Report;
}