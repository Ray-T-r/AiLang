package main

import "fmt"

func main() {
	s := 0
	for i := 1; i < 101; i++ {
		s += i
	}
	fmt.Println(s)
}
