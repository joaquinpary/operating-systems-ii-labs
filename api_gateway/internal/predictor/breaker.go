package predictor

import (
	"errors"
	"sync"
	"time"
)

// Circuit breaker states.
const (
	stateClosed   = iota // Normal operation — requests flow through.
	stateOpen            // Too many failures — reject immediately.
	stateHalfOpen        // Cooling off — allow one probe request.
)

// ErrBreakerOpen is returned when the circuit breaker is open.
var ErrBreakerOpen = errors.New("circuit breaker is open")

// CircuitBreaker implements a simple three-state circuit breaker.
//
// Closed  → counts consecutive failures; trips to Open after maxFailures.
// Open    → rejects all calls for resetTimeout, then transitions to HalfOpen.
// HalfOpen→ allows one probe call; success → Closed, failure → Open.
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
		// Timeout elapsed — transition to half-open and let this request probe.
		breaker.state = stateHalfOpen
		breaker.lock.Unlock()

	case stateHalfOpen:
		// Already probing — reject concurrent requests while the probe is in flight.
		breaker.lock.Unlock()
		return nil, ErrBreakerOpen

	default: // stateClosed
		breaker.lock.Unlock()
	}

	// Run the actual call outside the lock.
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

	// Success — reset.
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
