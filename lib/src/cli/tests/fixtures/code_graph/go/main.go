package main

import (
	str "strings"

	"example.com/demo/store"
)

func fill(s *store.Store) {
	s.Add(str.ToUpper("x"))
}

func main() {
	s := store.New()
	fill(s)
}
