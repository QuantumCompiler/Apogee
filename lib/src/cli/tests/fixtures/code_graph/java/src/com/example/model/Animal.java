package com.example.model;

public abstract class Animal {
    public abstract String sound();

    public String speak() {
        return name() + " says " + sound();
    }

    protected String name() {
        return getClass().getSimpleName();
    }
}
