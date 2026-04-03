package middleware

type TracingMiddleware struct{}

func NewTracingMiddleware() TracingMiddleware {
	return TracingMiddleware{}
}

func (middleware TracingMiddleware) Name() string {
	return "request-id"
}
