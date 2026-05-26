package main

import "fmt"

func main() {
	var s int64 = 0
	for i := int64(1); i <= 100000000; i++ {
		s += i
	}
	fmt.Println(s)
}
