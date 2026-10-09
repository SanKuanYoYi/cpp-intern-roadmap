#include "head_files.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <limits>

std::vector<Student> students; // Vector to store student records


void menu(){
    std::cout<<"The Student Manager System"<<std::endl;
    std::cout<<"--------------------------------"<<std::endl;
    std::cout<<"1. Add Student"<<std::endl;
    std::cout<<"2. Delete Student"<<std::endl;
    std::cout<<"3. Modify Student"<<std::endl;
    std::cout<<"4. Search Student"<<std::endl;
    std::cout<<"5. Show All Students"<<std::endl;
    std::cout<<"6. Sort Students With Score"<<std::endl;
    std::cout<<"7. Save In File"<<std::endl;
    std::cout<<"8. Load From File"<<std::endl;
    std::cout<<"9. Exit"<<std::endl;
    std::cout<<"--------------------------------"<<std::endl;
}

void student(){

    while(true){

        menu();
        int choice;
        std::cout << "Enter your choice: ";
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');// Clear the input buffer

        switch(choice){
            case 1:
                add_student();
                break;
            case 2:
                delete_student();
                break;
            case 3:
                modify_student();
                break;
            case 4:
                search_student();
                break;
            case 5:
                show_all_students();
                break;
            case 6:
                sort_students();
                break;
            case 7: {
                save_in_file("students.txt", students);
                break;
            }
            case 8: {
                load_from_file("students.txt", students);
                break;
            }
            case 9:
                std::cout<<"Exiting the system."<<std::endl;
                return;
            default:
                std::cout<<"Invalid choice. Please try again."<<std::endl;
        }
    }

}

void add_student(){

    std::string name;
    int age;
    double score;

    std::cout << "Enter student name: ";
    std::getline(std::cin, name);
    std::cout << "Enter student age: ";
    std::cin >> age;
    std::cout << "Enter student score: ";
    std::cin >> score;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');// Clear the input buffer

    students.push_back(Student(name, age, score));
    std::cout << "Student added successfully!" << std::endl;
}

void delete_student(){
    std::string name;
    std::cout << "Enter the name of the student to delete: ";
    std::getline(std::cin, name);

    for(auto it = students.begin(); it != students.end(); ++it) {
        if( it->get_name() == name) {
            students.erase(it);
            std::cout << "Student deleted successfully!" << std::endl;
            return;
        }
    }

    std::cout << "Student not found." << std::endl;
}

void modify_student(){
    std::string name;
    std::cout << "Enter the name of the student to modify: ";
    std::getline(std::cin, name);

    for(auto& student : students) {
        if(student.get_name() == name) {
            int new_age;
            double new_score;

            std::cout << "Enter new age: ";
            std::cin >> new_age;
            std::cout << "Enter new score: ";
            std::cin >> new_score;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');// Clear the input buffer

            student.set_age(new_age);
            student.set_score(new_score);
            std::cout << "Student modified successfully!" << std::endl;
            return;
        }
    }

    std::cout << "Student not found." << std::endl;
}

void search_student(){
    std::string name;
    std::cout << "Enter the name of the student to search: ";
    std::getline(std::cin, name);

    for(const auto& student : students) {
        if(student.get_name() == name) {
            std::cout << "Student found: " << student.get_name() << ", Age: " << student.get_age() << ", Score: " << student.get_score() << std::endl;
            return;
        }
    }

    std::cout << "Student not found." << std::endl;
}

void show_all_students(){
    std::cout << "There are " << students.size() << " students in the system." << std::endl;
    for(const auto& student : students) {
        if(!student.get_name().empty()) {
            std::cout << "Name: " << student.get_name() << ", Age: " << student.get_age() << ", Score: " << student.get_score() << std::endl;
        }
    }
}

void sort_students(){
    std::sort(students.begin(), students.end(), [](const Student& a, const Student& b) {
        return a.get_score() > b.get_score(); // Sort in descending order of score
    });

    std::cout << "Students sorted by score in descending order." << std::endl;
}

void save_in_file(const std::string& filename, const std::vector<Student>& students) {
    std::ofstream out(filename);
    if(!out.is_open()) {
        std::cerr << "Error opening file for writing." << std::endl;
        return;
    }

    for(const auto& student : students) {
        if(!student.get_name().empty()) {
            out << student.get_name() << " " << student.get_age() << " " << student.get_score() << std::endl;
        }
    }

    out.close();

    std::cout<< "Students saved to file successfully." << std::endl;
}

void load_from_file(const std::string& filename, std::vector<Student>& students) {
    std::ifstream in(filename);
    if(!in.is_open()) {
        std::cerr << "Error opening file for reading." << std::endl;
        return;
    }

    students.clear();
    std::string name;
    int age = 0;
    double score = 0.0;

    while(in >> name >> age >> score) {
        students.push_back(Student(name, age, score));
    }

    in.close();

    std::cout<< "Students loaded from file successfully." << std::endl;
}