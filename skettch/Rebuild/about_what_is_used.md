# class User

## The Rule of Zero

> In C++ suggests that classes should avoid defining custom destructors, copy/move constructors, or copy/move assignment operators, allowing the compiler to generate them automatically. This approach simplifies resource management by relying on types that already handle their own resources, like smart pointers or standard containers.

> Because the rule of RAII is telling that the freeing a resource should be made in scope. Because SERVER CLASS is opening the socket, class Server should be in charge of closing it.

> The User class shouldn't be in charge of receiving and sending messages because the main responsibility of the communication is on Server
