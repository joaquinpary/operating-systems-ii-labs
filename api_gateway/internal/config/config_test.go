package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestResolveDirPathFromParentDirectory(t *testing.T) {
	rootDir := t.TempDir()
	credentialsDir := filepath.Join(rootDir, "config", "gateway")
	if err := os.MkdirAll(credentialsDir, 0o755); err != nil {
		t.Fatalf("mkdir credentials dir: %v", err)
	}

	workingDir := filepath.Join(rootDir, "api_gateway")
	if err := os.MkdirAll(workingDir, 0o755); err != nil {
		t.Fatalf("mkdir working dir: %v", err)
	}

	previousDir, err := os.Getwd()
	if err != nil {
		t.Fatalf("get working dir: %v", err)
	}
	defer func() {
		if chdirErr := os.Chdir(previousDir); chdirErr != nil {
			t.Fatalf("restore working dir: %v", chdirErr)
		}
	}()

	if err := os.Chdir(workingDir); err != nil {
		t.Fatalf("change working dir: %v", err)
	}

	resolved := resolveDirPath("config/gateway")
	if resolved != credentialsDir {
		t.Fatalf("expected %q, got %q", credentialsDir, resolved)
	}
}

func TestResolveDirPathLeavesMissingRelativePathUntouched(t *testing.T) {
	rootDir := t.TempDir()

	previousDir, err := os.Getwd()
	if err != nil {
		t.Fatalf("get working dir: %v", err)
	}
	defer func() {
		if chdirErr := os.Chdir(previousDir); chdirErr != nil {
			t.Fatalf("restore working dir: %v", chdirErr)
		}
	}()

	if err := os.Chdir(rootDir); err != nil {
		t.Fatalf("change working dir: %v", err)
	}

	if resolved := resolveDirPath("config/gateway"); resolved != "config/gateway" {
		t.Fatalf("expected unresolved path, got %q", resolved)
	}
}
