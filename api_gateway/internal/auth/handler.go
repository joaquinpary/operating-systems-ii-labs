package auth

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/gofiber/fiber/v2"
	"github.com/golang-jwt/jwt/v5"
)

// credentials maps username → MD5 password hash.
type credentials map[string]string

// Handler handles the POST /login endpoint.
type Handler struct {
	creds     credentials
	jwtSecret string
}

// NewHandler creates a Handler by loading all .conf files from credentialsDir.
// Each file must contain "username = <value>" and "password = <md5hash>" lines.
func NewHandler(credentialsDir, jwtSecret string) (*Handler, error) {
	creds, err := loadCredentials(credentialsDir)
	if err != nil {
		return nil, fmt.Errorf("load credentials from %s: %w", credentialsDir, err)
	}
	log.Printf("auth: loaded %d gateway credentials from %s", len(creds), credentialsDir)
	return &Handler{creds: creds, jwtSecret: jwtSecret}, nil
}

// Login validates the username+password and returns a signed JWT.
func (handler *Handler) Login(ctx *fiber.Ctx) error {
	var body struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := ctx.BodyParser(&body); err != nil {
		return ctx.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "invalid request body"})
	}

	body.Username = strings.TrimSpace(body.Username)
	body.Password = strings.TrimSpace(body.Password)

	if body.Username == "" || body.Password == "" {
		return ctx.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "username and password are required"})
	}

	storedHash, exists := handler.creds[body.Username]
	if !exists || body.Password != storedHash {
		return ctx.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": "invalid credentials"})
	}

	now := time.Now()
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, jwt.MapClaims{
		"sub": body.Username,
		"iat": now.Unix(),
		"exp": now.Add(24 * time.Hour).Unix(),
	})

	signed, err := token.SignedString([]byte(handler.jwtSecret))
	if err != nil {
		log.Printf("auth: sign token for %s: %v", body.Username, err)
		return ctx.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "could not generate token"})
	}

	return ctx.JSON(fiber.Map{
		"token":      signed,
		"expires_in": 86400,
	})
}

// loadCredentials reads all .conf files in dir and returns a username→password map.
func loadCredentials(dir string) (credentials, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	creds := make(credentials)
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(entry.Name(), ".conf") {
			continue
		}

		username, password, err := parseConfFile(filepath.Join(dir, entry.Name()))
		if err != nil {
			log.Printf("auth: skip %s: %v", entry.Name(), err)
			continue
		}

		creds[username] = password
	}

	return creds, nil
}

// parseConfFile extracts "username" and "password" from a key=value conf file.
func parseConfFile(path string) (string, string, error) {
	file, err := os.Open(path)
	if err != nil {
		return "", "", err
	}
	defer file.Close()

	var username, password string
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		key, value, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}

		key = strings.TrimSpace(key)
		value = strings.TrimSpace(value)

		switch key {
		case "username":
			username = value
		case "password":
			password = value
		}
	}

	if err := scanner.Err(); err != nil {
		return "", "", err
	}

	if username == "" || password == "" {
		return "", "", fmt.Errorf("missing username or password in %s", path)
	}

	return username, password, nil
}
