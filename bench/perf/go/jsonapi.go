package main

import (
	"fmt"
	"strconv"
)

func main() {
	count := 0
	total := 2
	for i := 0; i < 50000; i++ {
		age := 18 + i%52
		if age >= 40 {
			rec := "{\"id\":" + strconv.Itoa(i) + ",\"name\":\"user_" + strconv.Itoa(i) + "\",\"age\":" + strconv.Itoa(age) + "}"
			if count > 0 {
				total += 1
			}
			total += len(rec)
			count += 1
		}
	}
	fmt.Println(count)
	fmt.Println(total)
}
