#include <iostream>
#include <iomanip>

#include "Halide.h"
#include "MyIRPrinter.h"
#include "float_comparison.h"

using namespace std;
using namespace Halide;
using namespace Halide::ConciseCasts;


#define USE_PARAMS 1

template<typename T>
void InitMatrix(Buffer<T>& buffer)
{
	for (int i = 0; i < buffer.width(); ++i)
	{
		for (int j = 0; j < buffer.height(); ++j)
		{
			float v = i + j * buffer.width();
			// v /= 512.0f;
			v = 1.0f;
			buffer(i, j) = static_cast<float16_t>(v);
		}
	}

}

template<typename T>
void PrintBuffer(const Buffer<T>& buffer)
{
	// Print the result.
	for (int y = 0; y < buffer.height(); y++)
	{
		for (int x = 0; x < buffer.width(); x++)
		{
			printf("%10.2f ", (float)buffer(x, y));
		}
		printf("\n");
	}
}

int main()
{
	try
	{
		// mk x kn
		const int M = 16;
		const int N = 16;
		const int K = 32;
		const int x_tile = 16;
		const int y_tile = 16;

		const int warp_size = 32;
		const int vec_size = 2;
		const int y_unroll = 1;
		const int r_unroll = 1;

		Target target = get_host_target();
		Target jitTarget = get_jit_target_from_environment();

		// Enable the CUDA target
		target.set_feature(Target::CUDA);
		target.set_feature(Target::CUDACapability70);

		// Enable debugging hopefully to see the CUDA API calls
		target.set_feature(Target::Debug);

		cout << "Halide Host Target: " << target << endl;
		cout << "Halide JIT Target : " << jitTarget << endl;

		Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("y1");
		Var xio("xio"), xii("xii"), yii("xii"), x_pair("x_pair"), xiio("xiio"), ty("ty");
		RVar rxo("rxo"), rxi("rxi");

#if USE_PARAMS
		ImageParam A(Float(16), 2, "A");
		ImageParam B(Float(16), 2, "B");
#else
		Func A("A"), B("B");
#endif

		// A(x, y) = 1;
		// B(x, y) = 1;
		// A(x, y) = select(y < rows / 2, 1, 2);
		// B(x, y) = select(x < rows / 2, 1, 2);
		// A(x, y) = x + y * rows;
		// B(x, y) = x + y * rows;

		Func matmul("matmul");
		RDom r(0, K);
		matmul(x, y) += f32((A(x, r)) * B(r, y));
		matmul(x, y) = A(x, y) + B(x, y);
		// matmul.trace_stores();

		/*Func out("out");
		out(x, y) = matmul(x, y);*/
		Func out = matmul;

		Var blockX("blockX"), blockY("blockY"), threadX("threadX"), threadY("threadY");
		out
			.update()
			// .gpu_tile(x, y, blockX, blockY, threadX, threadY, x_tile, y_tile)
			;

		/*out
			.tile(x, y, xi, yi, x_tile * vec_size * warp_size, y_tile * y_unroll)
			.split(yi, ty, yi, y_unroll)
			.vectorize(xi, vec_size)
			.split(xi, xio, xii, warp_size)
			.reorder(xio, yi, xii, ty, x, y)
			.unroll(xio)
			.unroll(yi)
			.gpu_blocks(x, y)
			.gpu_threads(ty)
			.gpu_lanes(xii)
			;*/

			//	// Schedule the matmul on the GPU in tileX x tileY tiles
			//	matmul
			//		.store_in(MemoryType::Register)
			//		.compute_at(out, x)
			//		.split(x, xo, xi, warp_size * vec_size, TailStrategy::RoundUp)
			//		.split(y, ty, y, y_unroll)
			//		.gpu_threads(ty)
			//		.unroll(xi, vec_size)
			//		.gpu_lanes(xi)
			//		.unroll(xo)
			//		.unroll(y)
			//		.update()
			//		.split(x, xo, xi, warp_size * vec_size, TailStrategy::RoundUp)
			//		.split(y, ty, y, y_unroll)
			//		.gpu_threads(ty)
			//		.unroll(xi, vec_size)
			//		.gpu_lanes(xi)
			//		.split(r.x, rxo, rxi, warp_size)
			//		.unroll(rxi, r_unroll)
			//		.reorder(xi, xo, y, rxi, ty, rxo)
			//		.unroll(xo)
			//		.unroll(y)
			//		;


			//matmul
			//	.compute_at(matmul.in(), x)
			//	.update()
			//	// .vectorize(x, 16)
			//	.gpu_tile(x, y, blockX, blockY, threadX, threadY, x_tile, y_tile)
			//	;

			// matmul.trace_stores();

		cout << "Matmul Loop Nest: " << std::endl;
		out.print_loop_nest();
		cout << endl << endl;

		/*Internal::Stmt s = Internal::lower_main_stmt({ out.function() }, out.name(), target);
		Internal::MyIRVisitor vis(std::cout);
		s.accept(&vis);*/

#if USE_PARAMS
		std::vector<Argument> args = { A, B };
#else
		std::vector<Argument> args;
#endif

		cout << "Compiling to HTML ... " << flush;
		out.compile_to_lowered_stmt("cuda.html", args, HTML, target);
		// matmul.compile_to_lowered_stmt("cuda.html", {}, HTML, target);
		cout << "done" << endl;

		cout << "Compiling to LLVM ... " << flush;
		//out.compile_to_llvm_assembly("cuda.ll", {}, target);
		cout << "done" << endl;

		cout << "Compiling to Assembly ... " << flush;
		//out.compile_to_assembly("cuda.S", {}, target);
		cout << "done" << endl;

		cout << "Jitting the code ... " << flush;
		out.compile_jit(target);
		cout << "done" << endl;

#if USE_PARAMS
		Buffer<float16_t> inputA(M, K);
		Buffer<float16_t> inputB(K, N);

		InitMatrix(inputA);
		InitMatrix(inputB);

		A.set(inputA);
		B.set(inputB);
#else
		Buffer<float> inputA = A.realize(rows, cols);
		Buffer<float> inputB = B.realize(rows, cols);
#endif
		cout << "Input A: (" << inputA.width() << " x " << inputA.height() << ")" << endl;
		PrintBuffer(inputA);
		cout << endl << endl;

		cout << "Input B: (" << inputB.width() << " x " << inputB.height() << ")" << endl;
		PrintBuffer(inputB);
		cout << endl << endl;

		// Run it
		Buffer<float> result = out.realize(M, N);

		cout << "Result: " << endl;
		PrintBuffer(result);
		cout << endl << endl;

		cout << "Comparing: " << endl;
		for (int i = 0; i < M; ++i)
		{
			for (int j = 0; j < N; ++j)
			{
				float acc = 0.0f;
				for (int k = 0; k < K; ++k)
				{
					float16_t a = inputA(i, k);
					float16_t b = inputB(k, j);
					// std::cout << a << " * " << b << " = " << (a * b) << std::endl;
					acc += static_cast<float>(a * b);
				}
				expect_almost_equal("result(i, j)", "acc", "aa", result(i, j), acc, 4000);

				/*float diff = std::abs(result(i, j)) - std::abs(acc);
				constexpr float epsilon = std::numeric_limits<float>::epsilon();
				if (diff >= epsilon*1000)
				{
					std::cout << "Error at (" << i << ", " << j << "). Expected: " << acc << " Got: " << result(i, j) << std::endl;
				}*/
			}
		}


	}
	catch (RuntimeError& e)
	{
		cout << e.what() << endl;
	}
	catch (CompileError& e)
	{
		cout << e.what() << endl;
	}
	catch (InternalError& e)
	{
		cout << e.what() << endl;
	}

	return 0;
}