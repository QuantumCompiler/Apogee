package com.example;

import com.example.model.Dog;
import java.util.List;

public class App {
    public static void main(String[] args) {
        Dog dog = new Dog();
        List<Dog> dogs = List.of(dog);
        System.out.println(dog.speak() + dogs.size());
    }
}
