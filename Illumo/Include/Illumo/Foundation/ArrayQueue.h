#pragma once

#include <cstddef>

template<typename T>
class ArrayQueue
{
public:
  ArrayQueue(std::size_t queueCapacity)
    : size(0)
    , capacity(queueCapacity)
    , head(0)
    , tail(0)
  {
    data = new T[queueCapacity];
  }
  ~ArrayQueue() { delete[] data; };

  inline void enqueue(const T& item)
  {
    if (size == capacity) {
      return;
    }
    data[tail] = item;
    tail = (tail + 1) % capacity;
    size++;
  };

  inline void dequeue(T& item)
  {
    if (size == 0) {
      return;
    }
    item = data[head];
    head = (head + 1) % capacity;
    size--;
  };

  inline bool isFull() const { return size == capacity; };

  inline bool isEmpty() const { return size == 0; };

  inline std::size_t getSize() const { return size; };

  inline std::size_t getCapacity() const { return capacity; };

  T& operator[](std::size_t index) { return data[index]; }

  const T& operator[](std::size_t index) const { return data[index]; }

private:
  T* data;
  std::size_t size;
  std::size_t capacity;
  std::size_t head;
  std::size_t tail;
};