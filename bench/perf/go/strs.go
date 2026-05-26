package main

import "fmt"

func main() {
	acc := "x"
	for i := 1; i <= 100000; i++ {
		acc = acc + "y"
	}
	fmt.Println(len(acc))
}
