package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"
)

func createNote(name string) {
	fmt.Println("creating note ", name)
}
func deleteNote(name string) {
	fmt.Println("deleting note ", name)
}
func editNote(name string) {
	fmt.Println("editing note ", name)
}
func viewNote(name string) {
	fmt.Println("viewing note ", name)
}

func main() {
	fmt.Println("HELLO")
	fmt.Println("Enter command (new / edit / view / del / quit):")
	reader := bufio.NewReader(os.Stdin)
	command, err := reader.ReadString('\n')
	if err != nil {
		fmt.Printf("invalid command. %v", err)
		panic(err)
	}

	command = strings.Trim(command, "\n")

	for {
		switch (command) {
		case "quit": 
			fmt.Println("Exiting")
			return
		case "new":
			fmt.Print("New note name? ")
			name, err := reader.ReadString('\n')
			if err != nil {
				panic(err)
			}
	
			createNote(name)
		case "del": 
			fmt.Print("Name of note to delete? ")
			name, err := reader.ReadString('\n')
			if err != nil {
				panic(err)
			}
	
			deleteNote(name)
		case "edit": 
			fmt.Print("Name of note to edit? ")
			name, err := reader.ReadString('\n')
			if err != nil {
				panic(err)
			}
	
			editNote(name)
		case "view":
			fmt.Print("Name of note to view? ")
			name, err := reader.ReadString('\n')
			if err != nil {
				panic(err)
			}
	
			viewNote(name)
		default:
			fmt.Println("Invalid command.")
		}

		fmt.Println("Enter command (new / edit / view / del / quit):")
		reader := bufio.NewReader(os.Stdin)
		command, err = reader.ReadString('\n')
		if err != nil {
			fmt.Printf("invalid command. %v", err)
			panic(err)
		}

		command = strings.Trim(command, "\n")
	}
}

/*
CLI for now
- password-encrypted notes
	- data symmetrically encrypted with a key derived from the master password

commands:
- create new note
- view notes (requires password)
- delete note

*/