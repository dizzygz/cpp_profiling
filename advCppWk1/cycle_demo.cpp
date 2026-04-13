/**
 * shared_ptr Cycle Memory Leak Demonstration
 *
 * This file shows:
 * 1. Memory leak caused by circular references with shared_ptr
 * 2. How to detect the leak using AddressSanitizer (ASan)
 * 3. Fix using weak_ptr to break the cycle
 *
 * COMPILE WITH LEAK DETECTION:
 *   g++ -std=c++17 -fsanitize=address -g cycle_demo.cpp -o cycle_demo
 *
 * Run: ./cycle_demo
 * Look for: "Direct leak" or "Indirect leak" in output
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// PART 1: PROBLEMATIC CODE - shared_ptr CYCLE
// ============================================================================

namespace Problematic
{

class Node : public std::enable_shared_from_this<Node>
{
 public:
   std::string name;
   std::shared_ptr<Node> parent; // BACK-POINTING POINTER (causes cycle!)
   std::vector<std::shared_ptr<Node>> children;
   static int instance_count;
   static int destructor_count;

   explicit Node(const std::string &n) : name(n)
   {
      ++instance_count;
      std::cout << "[Node Created] \"" << name << "\" (total: " << instance_count << ")\n";
   }

   ~Node()
   {
      ++destructor_count;
      std::cout << "[Node Destroyed] \"" << name << "\" (destroyed: " << destructor_count << ")\n";
   }

   void addChild(std::shared_ptr<Node> child)
   {
      children.push_back(child);
      child->parent = shared_from_this(); // Creates cycle!
   }
};

int Node::instance_count = 0;
int Node::destructor_count = 0;

void demonstrateLeak()
{
   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   PROBLEMATIC: shared_ptr Cycle (LEAK!)\n";
   std::cout << "========================================\n";
   std::cout << "\n";

   Node::instance_count = 0;
   Node::destructor_count = 0;

   std::cout << "Creating tree structure with cycle...\n\n";

   auto root = std::make_shared<Node>("Root");
   auto child1 = std::make_shared<Node>("Child1");
   auto child2 = std::make_shared<Node>("Child2");
   auto grandchild = std::make_shared<Node>("Grandchild");

   // Build tree - THIS CREATES CYCLES!
   root->addChild(child1);
   root->addChild(child2);
   child1->addChild(grandchild);

   std::cout << "\n--- About to exit function ---\n";
   std::cout << "Reference counts:\n";
   std::cout << "  root.use_count() = " << root.use_count() << "\n";
   std::cout << "  child1.use_count() = " << child1.use_count() << "\n";
   std::cout << "  grandchild.use_count() = " << grandchild.use_count() << "\n";

   root.reset();
   child1.reset();
   child2.reset();

   std::cout << "\nAfter reset() calls:\n";
   std::cout << "  grandchild.use_count() = " << grandchild.use_count() << "\n";

   grandchild.reset(); // This won't destroy grandchild because of cycle!

   std::cout << "\n--- Function exiting ---\n";

   std::cout << "\n[RESULT] Instances created: " << Node::instance_count << "\n";
   std::cout << "[RESULT] Instances destroyed: " << Node::destructor_count << "\n";

   if (Node::instance_count != Node::destructor_count)
   {
      std::cout << "[LEAK DETECTED!] " << (Node::instance_count - Node::destructor_count)
                << " nodes leaked!\n";
   }
}

} // namespace Problematic

// ============================================================================
// PART 2: FIXED CODE - Using weak_ptr
// ============================================================================

namespace Fixed
{

class Node : public std::enable_shared_from_this<Node>
{
 public:
   std::string name;
   std::weak_ptr<Node> parent; // CHANGE: weak_ptr instead of shared_ptr
   std::vector<std::shared_ptr<Node>> children;
   static int instance_count;
   static int destructor_count;

   explicit Node(const std::string &n) : name(n)
   {
      ++instance_count;
      std::cout << "[Node Created] \"" << name << "\" (total: " << instance_count << ")\n";
   }

   ~Node()
   {
      ++destructor_count;
      std::cout << "[Node Destroyed] \"" << name << "\" (destroyed: " << destructor_count << ")\n";
   }

   void addChild(std::shared_ptr<Node> child)
   {
      children.push_back(child);
      child->parent = weak_from_this(); // weak_from_this() - safe!
   }

   std::shared_ptr<Node> getParent() const
   {
      return parent.lock();
   }

   void printHierarchy(int indent = 0) const
   {
      for (int i = 0; i < indent; ++i)
         std::cout << "  ";
      std::cout << "- " << name;
      auto p = getParent();
      if (p)
      {
         std::cout << " (parent: " << p->name << ")";
      }
      std::cout << "\n";
      for (const auto &child : children)
      {
         child->printHierarchy(indent + 1);
      }
   }
};

int Node::instance_count = 0;
int Node::destructor_count = 0;

void demonstrateFix()
{
   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   FIXED: Using weak_ptr (NO LEAK!)\n";
   std::cout << "========================================\n";
   std::cout << "\n";

   Node::instance_count = 0;
   Node::destructor_count = 0;

   std::cout << "Creating same tree structure with weak_ptr...\n\n";

   auto root = std::make_shared<Node>("Root");
   auto child1 = std::make_shared<Node>("Child1");
   auto child2 = std::make_shared<Node>("Child2");
   auto grandchild = std::make_shared<Node>("Grandchild");

   // Build tree - weak_ptr breaks the cycle!
   root->addChild(child1);
   root->addChild(child2);
   child1->addChild(grandchild);

   std::cout << "\n--- Hierarchy ---\n";
   root->printHierarchy();

   std::cout << "\nReference counts:\n";
   std::cout << "  root.use_count() = " << root.use_count() << "\n";
   std::cout << "  child1.use_count() = " << child1.use_count() << "\n";
   std::cout << "  grandchild.use_count() = " << grandchild.use_count() << "\n";

   std::cout << "\nNote: parent pointers are weak - they don't contribute to count!\n";

   root.reset();

   std::cout << "\nAfter root.reset():\n";
   std::cout << "  child1.use_count() = " << child1.use_count() << "\n";

   auto parent = child1->getParent();
   std::cout << "  child1's parent: " << (parent ? parent->name : "nullptr (expired)") << "\n";

   child1.reset();
   child2.reset();
   grandchild.reset();

   std::cout << "\n--- Function exiting ---\n";

   std::cout << "\n[RESULT] Instances created: " << Node::instance_count << "\n";
   std::cout << "[RESULT] Instances destroyed: " << Node::destructor_count << "\n";

   if (Node::instance_count == Node::destructor_count)
   {
      std::cout << "[SUCCESS!] All nodes properly destroyed - NO LEAK!\n";
   }
   else
   {
      std::cout << "[FAILURE] " << (Node::instance_count - Node::destructor_count)
                << " nodes leaked!\n";
   }
}

} // namespace Fixed

// ============================================================================
// PART 3: Real-World Pattern - Employee/Manager with weak_ptr
// ============================================================================

namespace RealWorld
{

class Employee;

class Employee : public std::enable_shared_from_this<Employee>
{
 public:
   std::string name;
   std::weak_ptr<Employee> manager; // weak_ptr - doesn't prevent manager destruction

   Employee(const std::string &n) : name(n)
   {
      std::cout << "[Employee Created] " << name << "\n";
   }

   ~Employee()
   {
      std::cout << "[Employee Destroyed] " << name << "\n";
   }

   void setManager(std::shared_ptr<Employee> mgr)
   {
      manager = std::weak_ptr<Employee>(mgr);
   }

   void printManager() const
   {
      auto m = manager.lock();
      if (m)
      {
         std::cout << name << "'s manager: " << m->name << "\n";
      }
      else
      {
         std::cout << name << "'s manager: (no longer exists)\n";
      }
   }

   void addReport(std::shared_ptr<Employee> emp)
   {
      reports.push_back(emp);
      emp->manager = weak_from_this();
   }

   void printTeam() const
   {
      std::cout << "Team lead " << name << " -> Reports: ";
      for (const auto &e : reports)
      {
         std::cout << e->name << " ";
      }
      std::cout << "\n";
   }

 private:
   std::vector<std::shared_ptr<Employee>> reports;
};

void demonstratePattern()
{
   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Real-World: Employee/Manager Pattern\n";
   std::cout << "========================================\n";
   std::cout << "\n";

   auto ceo = std::make_shared<Employee>("CEO");
   auto vp1 = std::make_shared<Employee>("VP Engineering");
   auto vp2 = std::make_shared<Employee>("VP Sales");
   auto dev1 = std::make_shared<Employee>("Dev1");
   auto dev2 = std::make_shared<Employee>("Dev2");

   // CEO manages VPs
   ceo->addReport(vp1);
   ceo->addReport(vp2);

   // VP1 manages Dev1 and Dev2
   vp1->addReport(dev1);
   vp1->addReport(dev2);

   ceo->printTeam();
   vp1->printTeam();
   vp1->printManager();
   dev1->printManager();

   std::cout << "\n--- Deleting CEO (cascade delete!) ---\n";
   ceo.reset();

   std::cout << "\n--- Checking if employees still know their manager ---\n";
   vp1->printManager();
   dev1->printManager();

   std::cout << "\n--- Deleting remaining employees ---\n";
   vp1.reset();
   vp2.reset();
   dev1.reset();
   dev2.reset();

   std::cout << "\n[SUCCESS!] All employees properly destroyed\n";
}

} // namespace RealWorld

// ============================================================================
// PART 4: Direct Cycle Demonstration
// ============================================================================

namespace DirectCycle
{

// Simple base class for both scenarios
class PartnerBase
{
 public:
   std::string id;
   PartnerBase(const std::string &i) : id(i)
   {
   }
};

// A <-> B with shared_ptr (LEAK!)
class NodeA;
class NodeB : public PartnerBase
{
 public:
   std::shared_ptr<NodeA> partner; // shared_ptr creates cycle!
   NodeB(const std::string &i) : PartnerBase(i)
   {
      std::cout << "NodeB(" << id << ") created\n";
   }
   ~NodeB()
   {
      std::cout << "NodeB(" << id << ") destroyed\n";
   }
};

class NodeA : public PartnerBase
{
 public:
   std::shared_ptr<NodeB> partner;
   NodeA(const std::string &i) : PartnerBase(i)
   {
      std::cout << "NodeA(" << id << ") created\n";
   }
   ~NodeA()
   {
      std::cout << "NodeA(" << id << ") destroyed\n";
   }
};

// A <-> C with weak_ptr (FIXED!)
// Note: NodeC points back to NodeA using weak_ptr to break cycle
class NodeC : public PartnerBase
{
 public:
   std::weak_ptr<NodeA> partner; // weak_ptr breaks cycle!
   NodeC(const std::string &i) : PartnerBase(i)
   {
      std::cout << "NodeC(" << id << ") created\n";
   }
   ~NodeC()
   {
      std::cout << "NodeC(" << id << ") destroyed\n";
   }
   std::shared_ptr<NodeA> getPartner() const
   {
      return partner.lock();
   }
};

void demonstrateDirectCycle()
{
   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Direct Cycle: A<->B with shared_ptr\n";
   std::cout << "========================================\n\n";

   std::cout << "--- Creating NodeA and NodeB with cycle ---\n";
   auto a = std::make_shared<NodeA>("a");
   auto b = std::make_shared<NodeB>("b");

   a->partner = b;
   b->partner = a;

   std::cout << "\nCounts before reset:\n";
   std::cout << "  a.use_count() = " << a.use_count() << "\n";
   std::cout << "  b.use_count() = " << b.use_count() << "\n";

   std::cout << "\n--- Resetting a ---\n";
   a.reset();

   std::cout << "\nCounts after a.reset():\n";
   std::cout << "  b.use_count() = " << b.use_count() << "\n";

   std::cout << "\n--- Resetting b ---\n";
   b.reset();

   std::cout << "\n*** LEAK: Neither NodeA nor NodeB was destroyed! ***\n\n";
}

void demonstrateFixedCycle()
{
   std::cout << "========================================\n";
   std::cout << "   Fixed: Parent/Child with weak_ptr\n";
   std::cout << "========================================\n\n";

   // Create a tree where parent uses shared_ptr and child uses weak_ptr
   // This is the CORRECT pattern to avoid cycles

   struct FixedNode
   {
      std::string id;
      std::weak_ptr<FixedNode> parent;  // weak_ptr doesn't contribute to ref count
      std::shared_ptr<FixedNode> child; // shared_ptr does contribute
      FixedNode(const std::string &i) : id(i)
      {
         std::cout << "FixedNode(" << id << ") created\n";
      }
      ~FixedNode()
      {
         std::cout << "FixedNode(" << id << ") destroyed\n";
      }
   };

   auto parent = std::make_shared<FixedNode>("parent");
   auto child = std::make_shared<FixedNode>("child");

   parent->child = child;
   child->parent = parent; // weak_ptr - NO CYCLE!

   std::cout << "\nCounts:\n";
   std::cout << "  parent.use_count() = " << parent.use_count() << "\n";
   std::cout << "  child.use_count() = " << child.use_count() << "\n";

   std::cout << "\n--- Resetting parent ---\n";
   parent.reset();

   std::cout << "\nCounts after parent.reset():\n";
   std::cout << "  child.use_count() = " << child.use_count() << "\n";

   std::cout << "\n--- Resetting child ---\n";
   child.reset();

   std::cout << "\n*** SUCCESS: Both FixedNode destroyed! ***\n\n";
}

} // namespace DirectCycle

// ============================================================================
// MAIN
// ============================================================================

int main()
{
   std::cout << "========================================\n";
   std::cout << "   shared_ptr Cycle & weak_ptr Fix Demo\n";
   std::cout << "========================================\n";
   std::cout << "\n";
   std::cout << "Build with AddressSanitizer to detect leaks:\n";
   std::cout << "  g++ -std=c++17 -fsanitize=address -g -o demo demo.cpp\n";
   std::cout << "\n";

   Problematic::demonstrateLeak();
   Fixed::demonstrateFix();
   RealWorld::demonstratePattern();
   DirectCycle::demonstrateDirectCycle();
   DirectCycle::demonstrateFixedCycle();

   std::cout << "========================================\n";
   std::cout << "                    SUMMARY\n";
   std::cout << "========================================\n";
   std::cout << "  shared_ptr creates cycles when:\n";
   std::cout << "    - A points to B (owns)\n";
   std::cout << "    - B points back to A (owns)\n";
   std::cout << "  Result: Reference count never reaches 0 = MEMORY LEAK\n";
   std::cout << "\n";
   std::cout << "  Fix: Use weak_ptr for back-references\n";
   std::cout << "    - Parent owns children (shared_ptr)\n";
   std::cout << "    - Children observe parent (weak_ptr)\n";
   std::cout << "  Result: When parent is destroyed, children can detect\n";
   std::cout << "          via weak_ptr.expired() or use lock()\n";
   std::cout << "========================================\n";

   return 0;
}
