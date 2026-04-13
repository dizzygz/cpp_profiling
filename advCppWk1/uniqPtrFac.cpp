#include <iostream>
#include <memory>
#include <string>

// --- The Hierarchy ---

class Shape
{
 public:
   // CRITICAL: Virtual destructor ensures Derived cleanup
   virtual ~Shape()
   {
      std::cout << "Cleaning up Base Shape\n";
   }

   virtual void draw() const = 0; // Pure virtual
};

class Circle : public Shape
{
 public:
   ~Circle() override
   {
      std::cout << "Cleaning up Circle resources\n";
   }
   void draw() const override
   {
      std::cout << "Drawing a Circle ◯\n";
   }
};

class Square : public Shape
{
 public:
   ~Square() override
   {
      std::cout << "Cleaning up Square resources\n";
   }
   void draw() const override
   {
      std::cout << "Drawing a Square □\n";
   }
};

// --- The Factory ---

enum class ShapeType
{
   Circle,
   Square
};

std::unique_ptr<Shape> createShape(ShapeType type)
{
   if (type == ShapeType::Circle)
   {
      // We create a Circle, but return it as a Shape.
      // The unique_ptr handles the conversion automatically.
      return std::make_unique<Circle>();
   }
   else
   {
      return std::make_unique<Square>();
   }
}

// --- The Demonstration ---

int main()
{
   std::cout << "--- Creating Circle ---\n";
   std::unique_ptr<Shape> myShape = createShape(ShapeType::Circle);
   myShape->draw();

   std::cout << "\n--- Reassigning to Square (Old object is destroyed) ---\n";
   // Ownership transfer: The old Circle is deleted, and the new Square takes its place.
   myShape = createShape(ShapeType::Square);
   myShape->draw();

   std::cout << "\n--- Exiting Program ---\n";
   return 0;
}
