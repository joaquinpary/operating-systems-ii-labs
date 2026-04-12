package predictor

import (
	"errors"
	"sync"
	"time"
)

const (
	stateClosed = iota
	stateOpen
	stateHalfOpen
)

var ErrBreakerOpen = errors.New("circuit breaker is open")

type CircuitBreaker struct {
	lock             sync.Mutex
	state            int
	consecutiveFails int
	maxFailures      int
	resetTimeout     time.Duration
	lastOpenedAt     time.Time
}

// NewCircuitBreaker returns a breaker that opens after maxFailures consecutive
// errors and stays open for resetTimeout before allowing a probe.
func NewCircuitBreaker(maxFailures int, resetTimeout time.Duration) *CircuitBreaker {
	return &CircuitBreaker{
		maxFailures:  maxFailures,
		resetTimeout: resetTimeout,
	}
}

// Execute runs operation if the breaker allows it. On success the breaker resets to
// Closed; on failure the consecutive-failure counter increments and the breaker
// may trip to Open.
//
// When the breaker is Open, Execute returns (nil, ErrBreakerOpen) without
// calling operation.
func (breaker *CircuitBreaker) Execute(operation func() ([]byte, error)) ([]byte, error) {
	breaker.lock.Lock()

	switch breaker.state {
	case stateOpen:
		if time.Since(breaker.lastOpenedAt) < breaker.resetTimeout {
			breaker.lock.Unlock()
			return nil, ErrBreakerOpen
		}
		breaker.state = stateHalfOpen
		breaker.lock.Unlock()

	case stateHalfOpen:
		breaker.lock.Unlock()
		return nil, ErrBreakerOpen

	default:
		breaker.lock.Unlock()
	}

	data, err := operation()

	breaker.lock.Lock()
	defer breaker.lock.Unlock()

	if err != nil {
		breaker.consecutiveFails++
		if breaker.consecutiveFails >= breaker.maxFailures || breaker.state == stateHalfOpen {
			breaker.state = stateOpen
			breaker.lastOpenedAt = time.Now()
		}
		return nil, err
	}

	breaker.state = stateClosed
	breaker.consecutiveFails = 0
	return data, nil
}

// State returns a human-readable label for the current breaker state.
func (breaker *CircuitBreaker) State() string {
	breaker.lock.Lock()
	defer breaker.lock.Unlock()
	switch breaker.state {
	case stateOpen:
		return "open"
	case stateHalfOpen:
		return "half-open"
	default:
		return "closed"
	}
}
