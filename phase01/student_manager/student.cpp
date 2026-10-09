#include "student.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <limits>


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

void StudentManager::run() {
    while(true) {
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
                save_in_file("students.txt", students_);
                break;
            }
            case 8: {
                load_from_file("students.txt", students_);
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

void StudentManager::add_student(){

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

    students_.push_back(Student(name, age, score));
    std::cout << "Student added successfully!\n" << std::endl;
}

void StudentManager::delete_student(){
    std::string name;
    std::cout << "Enter the name of the student to delete: ";
    std::getline(std::cin, name);

    for(auto it = students_.begin(); it != students_.end(); ++it) {
        if( it->get_name() == name) {
            students_.erase(it);
            std::cout << "Student deleted successfully!\n" << std::endl;
            return;
        }
    }

    std::cout << "Student not found.\n" << std::endl;
}

void StudentManager::modify_student(){
    std::string name;
    std::cout << "Enter the name of the student to modify: ";
    std::getline(std::cin, name);

    for(auto& student : students_) {
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
            std::cout << "Student modified successfully!\n" << std::endl;
            return;
        }
    }

    std::cout << "Student not found.\n" << std::endl;
}

void StudentManager::search_student(){
    std::string name;
    std::cout << "Enter the name of the student to search: ";
    std::getline(std::cin, name);

    for(const auto& student : students_) {
        if(student.get_name() == name) {
            std::cout << "Student found: " << student.get_name() << ", Age: " << student.get_age() << ", Score: " << student.get_score() << std::endl << std::endl;
            return;
        }
    }

    std::cout << "Student not found.\n" << std::endl;
}

void StudentManager::show_all_students(){
    std::cout << "There are " << students_.size() << " students in the system.\n" << std::endl;
    for(const auto& student : students_) {
        if(!student.get_name().empty()) {
            std::cout << "Name: " << student.get_name() << ", Age: " << student.get_age() << ", Score: " << student.get_score() << std::endl << std::endl;
        }
    }
}

void StudentManager::sort_students(){
    std::sort(students_.begin(), students_.end(), [](const Student& a, const Student& b) {
        return a.get_score() > b.get_score(); // Sort in descending order of score
    });

    std::cout << "Students sorted by score in descending order.\n" << std::endl;
}

void StudentManager::save_in_file(const std::string& filename, const std::vector<Student>& students) {
    std::ofstream out(filename);
    if(!out.is_open()) {
        std::cerr << "Error opening file for writing.\n" << std::endl;
        return;
    }

    for(const auto& student : students) {
        if(!student.get_name().empty()) {
            out << student.get_name() << " " << student.get_age() << " " << student.get_score() << std::endl;
        }
    }

    out.close();

    std::cout<< "Students saved to file successfully.\n" << std::endl;
}

void StudentManager::load_from_file(const std::string& filename, std::vector<Student>& students) {
    std::ifstream in(filename);
    if(!in.is_open()) {
        std::cerr << "Error opening file for reading.\n" << std::endl;
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

    std::cout<< "Students loaded from file successfully.\n" << std::endl;
}