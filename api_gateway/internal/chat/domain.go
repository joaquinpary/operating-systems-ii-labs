package chat

import "time"

type Message struct {
	ID        string    `json:"id"`
	SenderID  string    `json:"sender_id"`
	TargetID  string    `json:"target_id"`
	Content   string    `json:"content"`
	CreatedAt time.Time `json:"created_at"`
}
