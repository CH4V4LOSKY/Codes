#pragma once
struct Servo {int position=-1,pin=-1;void write(int n){position=n;}void attach(int n){pin=n;}};
