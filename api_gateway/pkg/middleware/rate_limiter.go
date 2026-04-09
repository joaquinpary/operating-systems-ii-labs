package middleware

import (
	"fmt"
	"sync"
	"time"

	"github.com/gofiber/fiber/v2"
)

type tokenBucket struct {
	tokens     float64
	lastRefill time.Time
}

type RateLimiter struct {
	maxTokens      float64
	refillInterval time.Duration

	lock    sync.Mutex
	buckets map[string]*tokenBucket

	cleanupDone chan struct{}
}

// NewRateLimiter creates a rate limiter that allows maxRequests per interval
// for each unique client IP. A background goroutine prunes stale entries
// every 5 minutes.
func NewRateLimiter(maxRequests int, interval time.Duration) *RateLimiter {
	limiter := &RateLimiter{
		maxTokens:      float64(maxRequests),
		refillInterval: interval,
		buckets:        make(map[string]*tokenBucket),
		cleanupDone:    make(chan struct{}),
	}
	go limiter.cleanupLoop()
	return limiter
}

// Handler returns a Fiber middleware that enforces the rate limit.
// Requests exceeding the limit receive 429 Too Many Requests.
func (limiter *RateLimiter) Handler() fiber.Handler {
	return func(ctx *fiber.Ctx) error {
		clientIP := ctx.IP()

		if !limiter.allow(clientIP) {
			ctx.Set("Retry-After", fmt.Sprintf("%.0f", limiter.refillInterval.Seconds()))
			return ctx.Status(fiber.StatusTooManyRequests).JSON(fiber.Map{
				"error": "rate limit exceeded",
			})
		}

		return ctx.Next()
	}
}

// allow reports whether the client can spend one token from its bucket.
func (limiter *RateLimiter) allow(clientIP string) bool {
	limiter.lock.Lock()
	defer limiter.lock.Unlock()

	now := time.Now()
	bucket, exists := limiter.buckets[clientIP]

	if !exists {
		limiter.buckets[clientIP] = &tokenBucket{
			tokens:     limiter.maxTokens - 1,
			lastRefill: now,
		}
		return true
	}

	elapsed := now.Sub(bucket.lastRefill)
	refillAmount := (elapsed.Seconds() / limiter.refillInterval.Seconds()) * limiter.maxTokens
	bucket.tokens += refillAmount
	bucket.lastRefill = now

	if bucket.tokens > limiter.maxTokens {
		bucket.tokens = limiter.maxTokens
	}

	if bucket.tokens < 1.0 {
		return false
	}

	bucket.tokens--
	return true
}

// cleanupLoop removes IP entries that haven't been seen for more than
// twice the refill interval, preventing unbounded memory growth.
func (limiter *RateLimiter) cleanupLoop() {
	ticker := time.NewTicker(5 * time.Minute)
	defer ticker.Stop()

	for {
		select {
		case <-limiter.cleanupDone:
			return
		case <-ticker.C:
			limiter.pruneStaleEntries()
		}
	}
}

// pruneStaleEntries removes buckets that have been idle for too long.
func (limiter *RateLimiter) pruneStaleEntries() {
	limiter.lock.Lock()
	defer limiter.lock.Unlock()

	staleThreshold := time.Now().Add(-2 * limiter.refillInterval)
	for ip, bucket := range limiter.buckets {
		if bucket.lastRefill.Before(staleThreshold) {
			delete(limiter.buckets, ip)
		}
	}
}

// Close stops the background cleanup goroutine.
func (limiter *RateLimiter) Close() {
	close(limiter.cleanupDone)
}
