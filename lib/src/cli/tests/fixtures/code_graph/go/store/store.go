package store

import "fmt"

// Store keeps items.
type Store struct {
	items []string
}

// New makes an empty store.
func New() *Store {
	return &Store{}
}

// Add appends an item and reports the count.
func (s *Store) Add(item string) {
	s.items = append(s.items, item)
	fmt.Println(len(s.items))
}

// add is the unexported twin: a different function from Add.
func (s *Store) add(item string) {
	s.Add(item)
}
