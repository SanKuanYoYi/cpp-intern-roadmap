#ifndef HEAD_FILES_H
#define HEAD_FILES_H

#include <iostream>
#include <string>
#include <vector>

class Student {
private:
    std::string name;
    int age;
    double score;
public:
    Student() : name(""), age(0), score(0.0) {}
    Student(const std::string& n, int a, double s): name(n), age(a), score(s) {}

    std::string get_name() const { return name; }
    int get_age() const { return age; }
    double get_score() const { return score; }

    void set_name(const std::string& n) { name = n; }
    void set_age(int a) { age = a; }
    void set_score(double s) { score = s; }
};

extern std::vector<Student> students;

void student();
void menu();
void add_student();
void delete_student();
void modify_student();
void search_student();
void show_all_students();
void sort_students();
void save_in_file(const std::string& filename, const std::vector<Student>& students);
void load_from_file(const std::string& filename, std::vector<Student>& students);

#endif