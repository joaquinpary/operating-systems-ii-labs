package middleware

type JWTMiddleware struct {
	secret string
}

func NewJWTMiddleware(secret string) JWTMiddleware {
	return JWTMiddleware{secret: secret}
}

func (middleware JWTMiddleware) Enabled() bool {
	return middleware.secret != ""
}
