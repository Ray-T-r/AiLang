package main

import (
	"fmt"
	"strings"
)

func main() {
	seed := "the quick brown fox jumps over the lazy dog "
	text := strings.Repeat(seed, 500000)
	words := strings.Split(text, " ")
	counts := map[string]int{}
	for _, w := range words {
		counts[w]++
	}
	fmt.Println(len(counts))
	fmt.Println(counts["the"])
}
