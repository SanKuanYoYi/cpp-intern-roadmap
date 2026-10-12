#include <iostream>

struct Node {
    int value;
    Node* next;
};

void printList(Node* head) {
    Node* curr = head;
    while (curr != nullptr) {
        std::cout << curr->value << " ";
        curr = curr->next;
    }
    std::cout << std::endl;
}

int main() {
    Node* node1 = new Node{10, nullptr};
    Node* node2 = new Node{20, nullptr};
    
    node1->next = node2;
    
    std::cout << "List: ";
    printList(node1);
    
    // 故意制造的 Bug：空指针解引用
    Node* badNode = nullptr;
    std::cout << "Trying to access badNode->value: ";
    std::cout << badNode->value << std::endl;  // 程序会在这里 Segmentation fault
    
    delete node1;
    delete node2;
    return 0;
}