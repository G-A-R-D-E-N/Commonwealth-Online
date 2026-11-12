#include "ServerProtocol.h"

#include "F4TCompatibilityShim.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TSnapshotComponent.h"

#include <cassert>

namespace
{
	using CommonwealthOnline::Server::CompatibilityPublishStatus;
	using CommonwealthOnline::Server::CompatiblePublication;
	using CommonwealthOnline::Server::DirectComponentPublicationContract;
	using CommonwealthOnline::Server::DirectComponentPublicationPeer;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Compatibility::FamilyAdapter;
	using F4T::Replication::Family;

	constexpr Family kUndefinedFamily = static_cast<Family>(240);

	void DefineTransform(ProtocolHost& a_host)
	{
		F4T::Replication::ComponentRegistry registry;
		assert(registry.RegisterDefinition(Family::Transform, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable));
		const auto frozen = registry.Freeze();
		assert(frozen.has_value() && a_host.SetComponentDefinitions(*frozen));
	}

	CompatiblePublication Publication()
	{
		CompatiblePublication publication;
		publication.entityId = 1;
		publication.componentFamily = Family::Transform;
		publication.legacyFamily = "legacyTransform";
		publication.revision = 1;
		return publication;
	}

	void TestPublishRejectedWithoutCompatibilityMode()
	{
		ProtocolHost host;
		DefineTransform(host);
		assert(host.PublishCompatible(Publication()).status == CompatibilityPublishStatus::Rejected);
	}

	void TestRegisterAdapterRequiresDefinedFamily()
	{
		FamilyAdapter adapter;
		adapter.metadata.component.family = Family::Transform;
		ProtocolHost bare;
		assert(!bare.RegisterCompatibilityAdapter(adapter));

		ProtocolHost host;
		DefineTransform(host);
		adapter.metadata.component.family = kUndefinedFamily;
		assert(!host.RegisterCompatibilityAdapter(adapter));
		assert(host.PublishCompatible(Publication()).status == CompatibilityPublishStatus::Rejected);
	}

	void TestDirectContractValidation()
	{
		DirectComponentPublicationContract unconfigured;
		unconfigured.peers.push_back(DirectComponentPublicationPeer{ 5, true, false, { Family::Transform } });
		ProtocolHost bare;
		assert(!bare.SetDirectComponentPublicationContract(unconfigured));

		ProtocolHost host;
		DefineTransform(host);

		DirectComponentPublicationContract zeroPeer;
		zeroPeer.peers.push_back(DirectComponentPublicationPeer{ 0, true, false, { Family::Transform } });
		assert(!host.SetDirectComponentPublicationContract(zeroPeer));

		DirectComponentPublicationContract duplicatePeer;
		duplicatePeer.peers.push_back(DirectComponentPublicationPeer{ 5, true, false, { Family::Transform } });
		duplicatePeer.peers.push_back(DirectComponentPublicationPeer{ 5, false, true, {} });
		assert(!host.SetDirectComponentPublicationContract(duplicatePeer));

		DirectComponentPublicationContract undefinedFamily;
		undefinedFamily.peers.push_back(DirectComponentPublicationPeer{ 5, true, false, { kUndefinedFamily } });
		assert(!host.SetDirectComponentPublicationContract(undefinedFamily));

		DirectComponentPublicationContract valid;
		valid.peers.push_back(DirectComponentPublicationPeer{ 5, true, false, { Family::Transform } });
		assert(host.SetDirectComponentPublicationContract(valid));
	}
}

int main()
{
	TestPublishRejectedWithoutCompatibilityMode();
	TestRegisterAdapterRequiresDefinedFamily();
	TestDirectContractValidation();
	return 0;
}
