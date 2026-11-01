
#ifndef VERTEX_CACHE_H

#define VERTEX_CACHE_H

#include <vector>

// FIFO vertex cache simulation used by the stripifier.
//
// The stripifier asks InCache() for every vertex of every candidate strip, so InCache() has to be O(1):
// besides the FIFO itself a reference count per vertex index is kept, updated whenever an entry is
// added, evicted or overwritten. Behaviour is identical to scanning the FIFO, including the "empty slot"
// entry (-1), which is reported as present while at least one slot is still empty.
class VertexCache
{
	
public:
	
	VertexCache(int size) : numEntries(size), entries(size, -1), numEmpty(size)
	{
	}
		
	VertexCache() : VertexCache(16) {}

	VertexCache(const VertexCache &) = delete;
	VertexCache &operator=(const VertexCache &) = delete;
	
	bool InCache(int entry) const
	{
		if(entry == -1)
			return numEmpty > 0;

		if(entry < 0)
			return false;

		return entry < (int)refCounts.size() && refCounts[entry] > 0;
	}
	
	int AddEntry(int entry)
	{
		int removed;
		
		removed = entries[numEntries - 1];
		Unreference(removed);
		
		//push everything right one
		for(int i = numEntries - 2; i >= 0; i--)
		{
			entries[i + 1] = entries[i];
		}
		
		entries[0] = entry;
		Reference(entry);
		
		return removed;
	}

	void Clear()
	{
		for(int i = 0; i < numEntries; i++)
			entries[i] = -1;

		refCounts.assign(refCounts.size(), 0);
		numEmpty = numEntries;
	}
	
	void Copy(VertexCache* inVcache) const
	{
		for(int i = 0; i < numEntries; i++)
		{
			inVcache->Set(i, entries[i]);
		}
	}

	int At(int index) const { return entries[index]; }

	void Set(int index, int value)
	{
		Unreference(entries[index]);
		entries[index] = value;
		Reference(value);
	}

private:

	void Reference(int entry)
	{
		if(entry < 0)
		{
			if(entry == -1)
				++numEmpty;
			return;
		}

		if(entry >= (int)refCounts.size())
			refCounts.resize(entry + 1, 0);

		++refCounts[entry];
	}

	void Unreference(int entry)
	{
		if(entry < 0)
		{
			if(entry == -1)
				--numEmpty;
			return;
		}

		--refCounts[entry];
	}

	int numEntries;
	std::vector<int> entries;
	std::vector<int> refCounts;	// number of FIFO slots holding each vertex index
	int numEmpty;				// number of FIFO slots holding -1

};

#endif
