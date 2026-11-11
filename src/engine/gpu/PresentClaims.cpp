#include "engine/gpu/PresentClaims.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>

namespace drift::gl::present {

namespace {

struct Entry
{
    int claims = 0;
    void *producerFence = nullptr;
    QVector<void *> consumerFences;
};

QMutex &mutex()
{
    static QMutex m;
    return m;
}

QHash<unsigned, Entry> &entries()
{
    static QHash<unsigned, Entry> e;
    return e;
}

void dropIfEmpty(QHash<unsigned, Entry> &map, unsigned texture)
{
    auto it = map.find(texture);
    if (it != map.end() && it->claims <= 0 && !it->producerFence && it->consumerFences.isEmpty())
        map.erase(it);
}

} // namespace

void claim(unsigned texture)
{
    if (!texture)
        return;
    QMutexLocker lock(&mutex());
    ++entries()[texture].claims;
}

void release(unsigned texture, void *consumerFence)
{
    if (!texture)
        return;
    QMutexLocker lock(&mutex());
    auto &map = entries();
    auto it = map.find(texture);
    if (it == map.end()) {
        // A release with nothing to release would only happen after clearFences(); keep the
        // fence anyway so the producer still waits for it.
        if (consumerFence)
            map[texture].consumerFences.append(consumerFence);
        return;
    }
    it->claims = qMax(0, it->claims - 1);
    if (consumerFence)
        it->consumerFences.append(consumerFence);
    dropIfEmpty(map, texture);
}

bool isClaimed(unsigned texture)
{
    if (!texture)
        return false;
    QMutexLocker lock(&mutex());
    const auto it = entries().constFind(texture);
    return it != entries().cend() && it->claims > 0;
}

void setProducerFence(unsigned texture, void *fence)
{
    if (!texture)
        return;
    QMutexLocker lock(&mutex());
    entries()[texture].producerFence = fence;
    dropIfEmpty(entries(), texture);
}

void clearProducerFence(unsigned texture)
{
    setProducerFence(texture, nullptr);
}

void *producerFence(unsigned texture)
{
    if (!texture)
        return nullptr;
    QMutexLocker lock(&mutex());
    const auto it = entries().constFind(texture);
    return it != entries().cend() ? it->producerFence : nullptr;
}

std::vector<void *> takeConsumerFences(unsigned texture)
{
    std::vector<void *> out;
    if (!texture)
        return out;
    QMutexLocker lock(&mutex());
    auto &map = entries();
    auto it = map.find(texture);
    if (it == map.end())
        return out;
    out.assign(it->consumerFences.cbegin(), it->consumerFences.cend());
    it->consumerFences.clear();
    dropIfEmpty(map, texture);
    return out;
}

void clearFences()
{
    QMutexLocker lock(&mutex());
    auto &map = entries();
    for (auto it = map.begin(); it != map.end();) {
        it->producerFence = nullptr;
        it->consumerFences.clear();
        if (it->claims <= 0)
            it = map.erase(it);
        else
            ++it;
    }
}

} // namespace drift::gl::present
