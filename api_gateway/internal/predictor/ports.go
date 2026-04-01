package predictor

import "context"

type Service interface {
	PredictFlow(ctx context.Context, request PredictionRequest) (PredictionResult, error)
	PredictCircuit(ctx context.Context, request PredictionRequest) (PredictionResult, error)
}

type Transport interface {
	RequestFlowPrediction(ctx context.Context, request PredictionRequest) (PredictionResult, error)
	RequestCircuitPrediction(ctx context.Context, request PredictionRequest) (PredictionResult, error)
}
