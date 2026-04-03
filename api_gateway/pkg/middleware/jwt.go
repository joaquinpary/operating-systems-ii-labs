package middleware

import (
	"fmt"
	"strings"

	"github.com/gofiber/fiber/v2"
	"github.com/golang-jwt/jwt/v5"
)

type JWTMiddleware struct {
	secret string
}

func NewJWTMiddleware(secret string) JWTMiddleware {
	return JWTMiddleware{secret: secret}
}

func (middleware JWTMiddleware) Enabled() bool {
	return middleware.secret != ""
}

func (middleware JWTMiddleware) Handler() fiber.Handler {
	return func(ctx *fiber.Ctx) error {
		if !middleware.Enabled() {
			return ctx.Next()
		}

		authorization := strings.TrimSpace(ctx.Get(fiber.HeaderAuthorization))
		tokenString, ok := strings.CutPrefix(authorization, "Bearer ")
		if !ok || strings.TrimSpace(tokenString) == "" {
			return ctx.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": "missing bearer token"})
		}

		token, err := jwt.Parse(tokenString, func(token *jwt.Token) (any, error) {
			if _, ok := token.Method.(*jwt.SigningMethodHMAC); !ok {
				return nil, fmt.Errorf("unexpected signing method: %s", token.Method.Alg())
			}

			return []byte(middleware.secret), nil
		})
		if err != nil || !token.Valid {
			return ctx.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": "invalid token"})
		}

		if claims, ok := token.Claims.(jwt.MapClaims); ok {
			ctx.Locals("user", claims)
		}

		return ctx.Next()
	}
}
