package server

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"time"

	"lora-chads/api-gateway/internal/platform/config"
)

type Server struct {
	cfg        config.Config
	httpServer *http.Server
	mux        *http.ServeMux
	logger     *log.Logger
	shipments  http.Handler
	chat       http.Handler
	predictor  http.Handler
}

func New(cfg config.Config, shipments, chat, predictor http.Handler) *Server {
	mux := http.NewServeMux()

	srv := &Server{
		cfg:       cfg,
		mux:       mux,
		logger:    log.Default(),
		shipments: shipments,
		chat:      chat,
		predictor: predictor,
	}

	srv.registerRoutes()
	srv.httpServer = &http.Server{
		Addr:         cfg.Address(),
		Handler:      mux,
		ReadTimeout:  cfg.ReadTimeout,
		WriteTimeout: cfg.WriteTimeout,
		IdleTimeout:  cfg.IdleTimeout,
	}

	return srv
}

func (srv *Server) Run(ctx context.Context) error {
	errCh := make(chan error, 1)

	go func() {
		srv.logger.Printf("api gateway listening on %s and targeting core TCP %s", srv.cfg.Address(), srv.cfg.CoreAddress())
		if err := srv.httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- fmt.Errorf("listen and serve: %w", err)
			return
		}

		errCh <- nil
	}()

	select {
	case <-ctx.Done():
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		srv.logger.Println("shutdown signal received")
		if err := srv.httpServer.Shutdown(shutdownCtx); err != nil {
			return fmt.Errorf("shutdown http server: %w", err)
		}

		return nil
	case err := <-errCh:
		return err
	}
}

func (srv *Server) registerRoutes() {
	srv.mux.Handle("/api/v1/shipments", srv.shipments)
	srv.mux.Handle("/api/v1/chat", srv.chat)
	srv.mux.Handle("/api/v1/predictor", srv.predictor)
	srv.mux.HandleFunc("/healthz", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]string{
			"status":  "ok",
			"service": "api_gateway",
		})
	})
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)

	if err := json.NewEncoder(w).Encode(payload); err != nil {
		http.Error(w, http.StatusText(http.StatusInternalServerError), http.StatusInternalServerError)
	}
}
