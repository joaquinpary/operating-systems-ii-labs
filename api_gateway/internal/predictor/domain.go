package predictor

type PredictionRequest struct {
	Source string `json:"source"`
	Sink   string `json:"sink"`
}

type PredictionResult struct {
	Algorithm      string  `json:"algorithm"`
	ExecutionTime  float64 `json:"execution_time_ms"`
	NodeCount      int     `json:"node_count"`
	PredictedValue int     `json:"predicted_value"`
	UseOpenMP      bool    `json:"use_openmp"`
}
