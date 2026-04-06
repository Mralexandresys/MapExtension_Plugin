#pragma once

namespace MapStateRuntime
{
namespace Detail
{
	bool StartHttpServer();
	void StopHttpServer();
	void HandleHttpProcessDetach(bool processTerminating);
}
}
