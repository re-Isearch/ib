#ifndef IB_PEERRESOLVER_HXX
#define IB_PEERRESOLVER_HXX

#include "idbobj.hxx"

// Cache field descriptors, mapped tables and exact search regions for one
// operation. Reuse a complete resolution only within the intersection of its
// exact field-search regions and for the same incoming last-field cursor.
// Keep the scalar resolver's field order and containing-run selection.
class MAPPED_PEER_RESOLVER : public PEER_RESOLVER
{
public:
  MAPPED_PEER_RESOLVER(IDBOBJ& db, FCACHE& cache, size_t& lastPeerField);
  ~MAPPED_PEER_RESOLVER() override;
  FC Resolve(const FC& hit) override;
  PEER_RESOLVER_STATISTICS GetStatistics() const override;
private:
  struct STATE;
  std::unique_ptr<STATE> State;
};

#endif
