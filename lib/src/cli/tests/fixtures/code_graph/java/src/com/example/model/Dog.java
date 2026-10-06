package com.example.model;

public class Dog extends Animal implements Comparable<Dog> {
    @Override
    public String sound() {
        return "woof";
    }

    @Override
    public int compareTo(Dog other) {
        return 0;
    }
}
